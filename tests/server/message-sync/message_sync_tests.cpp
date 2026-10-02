#include "../../../common/message/MessageReceipts.h"
#include "../../../common/message/MessagePersistence.h"
#include "../../../common/message/GroupMembership.h"
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
#include <map>
#include <iomanip>
#include <sstream>
#include "../../../ChatServer/ChatServer/MySqlMessageCommitAdapter.h"
#include "../../../common/resource/ResourceCatalog.h"

/** 初始化并执行消息同步数据库测试，返回真实测试状态。 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {
/** 连接显式指定的隔离测试库并限制锁等待时间，未指定端点则拒绝。 */
std::unique_ptr<sql::Connection> Connect() {
    const auto* endpoint = std::getenv("MESSAGE_SYNC_TEST_MYSQL");
    if (!endpoint) throw std::runtime_error("requires isolated MESSAGE_SYNC_TEST_MYSQL");
    std::unique_ptr<sql::Connection> connection(sql::mysql::get_mysql_driver_instance()->connect(endpoint, "root", ""));
    connection->setSchema("message_sync_test");
    std::unique_ptr<sql::Statement> statement(connection->createStatement());
    statement->execute("SET SESSION innodb_lock_wait_timeout=5");
    return connection;
}
/** 为样本名称分配进程内稳定的合法 UUID。 */
std::string Uuid(const std::string& name) {
    static std::map<std::string, int> identifiers;
    if (!identifiers.count(name)) identifiers[name] = static_cast<int>(identifiers.size()) + 1;
    std::ostringstream value;
    value << "00000000-0000-4000-8000-" << std::setfill('0') << std::setw(12) << identifiers[name];
    return value.str();
}
/** 经生产提交器保存文本批次并返回消息编号，提交失败抛异常。 */
std::vector<int> SaveText(sql::Connection& connection, int sender, int recipient, int chat,
                         message_commit::Batch messages) {
    for (auto& message : messages) message.first = Uuid(message.first);
    message_commit::MySqlMessageCommitAdapter adapter(connection);
    const auto result = message_commit::Commit(adapter, {sender}, sender, recipient, chat, messages,
        std::chrono::steady_clock::now() + std::chrono::seconds(5));
    if (!result.IsSuccess()) throw std::runtime_error("text commit failed");
    std::vector<int> ids;
    for (const auto& item : result.items) ids.push_back(item.message_id);
    return ids;
}
/** 为固定参与者读取受字节上限约束的增量页。 */
Json::Value Page(sql::Connection& connection, int chat, std::int64_t after, std::size_t limit = 2048) {
    Json::Value result;
    result["mode"] = "sync_v1";
    result["error"] = 0;
    result["chat_id"] = chat;
    result["request_id"] = "test-request";
    result["after_id"] = Json::Int64(after);
    messaging::SyncPage(connection, 7, chat, after, result, limit);
    return result;
}
}

/** 验证相同身份重试幂等、冲突批次整批回滚及伪造参与者拒绝。 */
TEST(MessageSync, RetryConflictAndBatchRollback) {
    auto connection = Connect();
    const auto ids = SaveText(*connection, 7, 8, 101, {{"a", "first"}});
    EXPECT_EQ(ids, SaveText(*connection, 7, 8, 101, {{"a", "first"}}));
    EXPECT_THROW(SaveText(*connection, 7, 8, 101, {{"b", "rolled back"}, {"a", "conflict"}}), std::exception);
    EXPECT_THROW(SaveText(*connection, 9, 8, 101, {{"forged", "no"}}), std::exception);
    const auto page = Page(*connection, 101, 0);
    ASSERT_EQ(page["msgs"].size(), 1);
    EXPECT_EQ(page["msgs"][0]["msg_uuid"].asString(), Uuid("a"));
    EXPECT_TRUE(Page(*connection, 101, ids[0])["msgs"].empty());
    EXPECT_TRUE(connection->getAutoCommit());
}

/** 验证按字节限制分页无遗漏，并保留资源消息身份。 */
TEST(MessageSync, IncrementalByteBoundedPagesAndResourceIdentity) {
    auto connection = Connect();
    std::vector<std::pair<std::string, std::string>> messages;
    for (int index = 0; index < 9; ++index) messages.emplace_back("page-" + std::to_string(index), std::string(500, 'x'));
    const auto ids = SaveText(*connection, 7, 9, 102, messages);
    std::int64_t cursor = 0;
    std::vector<int> received;
    bool more = true;
    for (int count = 0; more && count < 20; ++count) {
        const auto page = Page(*connection, 102, cursor);
        ASSERT_LE(messaging::CompactJson(page).size(), 2048);
        for (const auto& message : page["msgs"]) received.push_back(message["message_id"].asInt());
        ASSERT_GT(page["next_cursor"].asInt64(), cursor);
        cursor = page["next_cursor"].asInt64();
        more = page["load_more"].asBool();
    }
    EXPECT_FALSE(more);
    EXPECT_EQ(received, ids);
    auto empty = Page(*connection, 102, cursor);
    EXPECT_TRUE(empty["msgs"].empty());
    EXPECT_EQ(empty["next_cursor"].asInt64(), cursor);
    Json::Value denied;
    EXPECT_THROW(messaging::SyncPage(*connection, 99, 102, 0, denied, 2048), std::exception);

    resource::ResourceCatalog catalog(std::getenv("MESSAGE_SYNC_TEST_MYSQL"), "root", "", "message_sync_test");
    const std::string resourceId = "00000000-0000-4000-8000-000000000099";
    catalog.Publish(resourceId, 7, "image.png", "image/png", 123, std::string(64, 'a'));
    EXPECT_THROW(catalog.CommitMessage(7, 10, 103, Uuid("a"), resourceId), std::exception);
    catalog.CommitMessage(7, 10, 103, Uuid("resource"), resourceId);
    EXPECT_THROW(SaveText(*connection, 7, 10, 103, {{"resource", "conflict"}}), std::exception);
    auto resource = Page(*connection, 103, 0);
    ASSERT_EQ(resource["msgs"].size(), 1);
    EXPECT_EQ(resource["msgs"][0]["msg_uuid"].asString(), Uuid("resource"));
    std::unique_ptr<sql::Statement> state(connection->createStatement());
    state->execute("UPDATE private_chat SET relationship_active=FALSE,relationship_revision=2 WHERE chat_id=103");
    EXPECT_TRUE(catalog.CanRead(10,resourceId));
    EXPECT_EQ(catalog.CommitMessage(7,10,103,Uuid("resource"),resourceId).id,resource["msgs"][0]["message_id"].asInt());
    EXPECT_THROW(catalog.CommitMessage(7,10,103,Uuid("resource-deleted"),resourceId),messaging::PrivateSendDenied);
    state->execute("UPDATE private_chat SET relationship_active=TRUE,relationship_revision=3 WHERE chat_id=103");
    EXPECT_THROW(catalog.CommitMessage(7,10,103,Uuid("resource-deleted"),resourceId,0,1),messaging::PrivateSendDenied);
    EXPECT_GT(catalog.CommitMessage(7,10,103,Uuid("resource-current"),resourceId,0,3).id,0);
    EXPECT_TRUE(catalog.CanRead(10,resourceId));
}

/** 验证同步读取等待尚未提交的写事务，游标不能越过它。 */
TEST(MessageSync, CursorCannotPassAnUncommittedWriter) {
    auto first = Connect();
    first->setAutoCommit(false);
    messaging::LockConversation(*first, 104, 7, 11);
    std::unique_ptr<sql::Statement> statement(first->createStatement());
    statement->execute("INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES(104,7,11,'held',0)");
    // Production sync uses the same row lock. Observe an actual DB lock wait,
    // rather than inferring ordering from a fixed sleep.
    auto reading = std::async(std::launch::async, /** 使用独立数据库会话读取被写事务阻塞的同步页。 */ [] {
        auto second = Connect();
        return Page(*second, 104, 0);
    });
    auto observer = Connect();
    std::unique_ptr<sql::Statement> probe(observer->createStatement());
    bool blocked = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && !blocked) {
        std::unique_ptr<sql::ResultSet> rows(probe->executeQuery("SELECT COUNT(*) FROM performance_schema.data_lock_waits"));
        blocked = rows->next() && rows->getInt(1) > 0;
        std::this_thread::yield();
    }
    first->commit();
    first->setAutoCommit(true);
    ASSERT_EQ(reading.wait_for(std::chrono::seconds(6)), std::future_status::ready);
    const auto page = reading.get();
    EXPECT_TRUE(blocked);
    ASSERT_EQ(page["msgs"].size(), 1);
    EXPECT_EQ(page["msgs"][0]["content"].asString(), "held");
}

namespace {
/** 构造并执行回执上报或 revision 同步请求，返回协议响应。 */
Json::Value Receipt(sql::Connection& connection, int uid, int chat, const std::vector<int>& ids,
                    const std::string& level, std::int64_t after = 0) {
    Json::Value request;
    request["version"] = 1; request["chat_id"] = chat; request["request_id"] = "receipt-integration";
    const bool report = !ids.empty();
    if (report) {
        for (int id : ids) {
            Json::Value item;
            item["message_id"] = id; item["level"] = level;
            request["items"].append(item);
        }
    } else request["after_revision"] = std::to_string(after);
    Json::Value response;
    response["version"] = 1; response["chat_id"] = chat;
    response["request_id"] = request["request_id"]; response["error"] = 0;
    int peer = 0;
    messaging::ReceiptRequest(connection, uid, request, report, response, peer);
    return response;
}
}

/** 验证回执参与权限、单调升级及持久化结果。 */
TEST(MessageSync, ReceiptsAreAuthorizedMonotonicAndDurable) {
    auto connection = Connect();
    const auto ids = SaveText(*connection, 7, 8, 101, {{"receipt-first", "receipt one"}, {"receipt-second", "receipt two"}});
    EXPECT_THROW(Receipt(*connection, 7, 101, ids, "read"), messaging::ReceiptError);
    EXPECT_THROW(Receipt(*connection, 9, 101, ids, "read"), messaging::ReceiptError);
    EXPECT_THROW(Receipt(*connection, 8, 101, {ids[0], INT_MAX}, "read"), messaging::ReceiptError);
    EXPECT_TRUE(Receipt(*connection, 7, 101, {}, "")["items"].empty());
    const auto delivered = Receipt(*connection, 8, 101, ids, "delivered");
    EXPECT_EQ(delivered["items"].size(), 2);
    EXPECT_EQ(delivered["latest_revision"].asString(), "2");
    EXPECT_EQ(Receipt(*connection, 8, 101, ids, "delivered"), delivered);
    const auto read = Receipt(*connection, 8, 101, {ids[0]}, "read");
    EXPECT_EQ(read["latest_revision"].asString(), "3");
    EXPECT_EQ(read["items"][0]["delivered_at"], delivered["items"][0]["delivered_at"]);
    EXPECT_EQ(Receipt(*connection, 8, 101, {ids[0]}, "delivered")["items"][0]["level"].asString(), "read");
    auto reopened = Connect();
    const auto increment = Receipt(*reopened, 7, 101, {}, "", 2);
    ASSERT_EQ(increment["items"].size(), 1);
    EXPECT_EQ(increment["items"][0]["message_id"].asInt(), ids[0]);
    EXPECT_EQ(increment["items"][0]["level"].asString(), "read");
    EXPECT_EQ(increment["next_revision"].asString(), "3");
}

/** 验证分页期间已读升级移动到新 revision 后仍能被后续页读取。 */
TEST(MessageSync, ReceiptPageCannotSkipAnUpgrade) {
    auto connection = Connect();
    std::vector<int> ids;
    for (int i = 0; i < 18; ++i) {
        auto saved = SaveText(*connection, 7, 9, 102, {{"receipt-page-" + std::to_string(i), "paged"}});
        ids.push_back(saved[0]);
        Receipt(*connection, 9, 102, saved, "delivered");
    }
    auto page = Receipt(*connection, 7, 102, {}, "");
    EXPECT_TRUE(page["load_more"].asBool());
    EXPECT_LE(messaging::CompactJson(page).size(), 2048);
    Receipt(*connection, 9, 102, {ids[0]}, "read"); // Row moves ahead of the page already fetched.
    bool saw_upgrade = false;
    for (int i = 0; i < 10 && page["load_more"].asBool(); ++i) {
        page = Receipt(*connection, 7, 102, {}, "", messaging::ReceiptRevision(page["next_revision"]));
        for (const auto& row : page["items"])
            if (row["message_id"].asInt() == ids[0] && row["level"].asString() == "read") saw_upgrade = true;
        EXPECT_LE(messaging::CompactJson(page).size(), 2048);
    }
    EXPECT_TRUE(saw_upgrade);
    EXPECT_FALSE(page["load_more"].asBool());
    EXPECT_EQ(page["next_revision"].asString(), "19");
}

/** 验证三人群共享单份正文，非成员拒绝且同步不能越过未提交的群消息。 */
TEST(MessageSync, GroupMembershipAndCommitOrdering) {
    auto first = Connect();
    std::unique_ptr<sql::Statement> statement(first->createStatement());
    statement->execute("INSERT INTO chat(chat_id,type) VALUES(301,'group')");
    statement->execute("INSERT INTO group_chat(chat_id,name) VALUES(301,'Three members')");
    statement->execute("INSERT INTO group_chat_member(chat_id,user_id) VALUES(301,7),(301,8),(301,9)");
    message_commit::MySqlMessageCommitAdapter adapter(*first);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const message_commit::Batch batch{{Uuid("group-member"), "shared text"}};
    const auto sent = message_commit::Commit(adapter, {8}, 8, 0, 301, batch, deadline, true);
    ASSERT_TRUE(sent.IsSuccess());
    const auto retry = message_commit::Commit(adapter, {8}, 8, 0, 301, batch, deadline, true);
    ASSERT_TRUE(retry.IsSuccess());
    EXPECT_EQ(sent.items[0].message_id, retry.items[0].message_id);
    EXPECT_FALSE(message_commit::Commit(adapter, {10}, 10, 0, 301, batch, deadline, true).IsSuccess());
    Json::Value denied, third;
    EXPECT_THROW(messaging::SyncPage(*first, 10, 301, 0, denied, 2048), std::exception);
    messaging::SyncPage(*first, 9, 301, 0, third, 2048);
    ASSERT_EQ(third["msgs"].size(), 1);
    EXPECT_EQ(third["msgs"][0]["send_id"].asInt(), 8);
    first->setAutoCommit(false);
    messaging::LockConversation(*first, 301, 8);
    statement->execute("INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES(301,8,0,'held group',0)");
    auto reading = std::async(std::launch::async,
        /** 在另一连接补拉群历史，必须等待当前写事务提交。 */
        [] { auto second = Connect(); return Page(*second, 301, 0); });
    auto observer = Connect();
    std::unique_ptr<sql::Statement> probe(observer->createStatement());
    bool blocked = false;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end && !blocked) {
        std::unique_ptr<sql::ResultSet> rows(probe->executeQuery("SELECT COUNT(*) FROM performance_schema.data_lock_waits"));
        blocked = rows->next() && rows->getInt(1) > 0;
        std::this_thread::yield();
    }
    first->commit(); first->setAutoCommit(true);
    ASSERT_EQ(reading.wait_for(std::chrono::seconds(6)), std::future_status::ready);
    auto page = reading.get();
    EXPECT_TRUE(blocked);
    ASSERT_EQ(page["msgs"].size(), 2);
    EXPECT_EQ(page["msgs"][1]["content"].asString(), "held group");
}

/** 验证动态成员的历史边界、权限、幂等及群资源引用授权共用同一事务事实。 */
TEST(MessageSync, DynamicGroupLifecycleAndResources) {
    auto db = Connect();
    messaging::GroupQuery setup(*db, "INSERT INTO chat(chat_id,type) VALUES(302,'group')", {}, false);
    messaging::GroupQuery group(*db, "INSERT INTO group_chat(chat_id,name,owner_uid,creator_uid,original_name) VALUES(302,'dynamic',7,7,'dynamic')", {}, false);
    messaging::GroupQuery members(*db, "INSERT INTO group_chat_member(chat_id,user_id,role) VALUES(302,7,1),(302,8,0)", {}, false);
    messaging::GroupQuery friends(*db, "INSERT IGNORE INTO friend(self_id,other_id,backname) VALUES(7,9,''),(7,10,'')", {}, false);
    message_commit::MySqlMessageCommitAdapter adapter(*db); adapter.SetGroupEpoch(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    auto send = /** @brief 按当前群成员代次提交不可变 UUID 的文字。 */ [&](const std::string& name) {
        return message_commit::Commit(adapter,{7},7,0,302,{{Uuid(name),name}},deadline,true);
    };
    const auto old = send("before-join"); ASSERT_TRUE(old.IsSuccess());
    Json::Value add;
    add["chat_id"]=302; add["request_id"]=Uuid("add-nine"); add["expected_revision"]="1"; add["operation"]="add"; add["members"].append(9);
    Json::Value result;
    for (const int actor : {8,10}) for (const auto* operation : {"add","remove","transfer","rename","dissolve"}) {
        auto denied=add; denied["operation"]=operation; denied["target_uid"]=8; denied["name"]="denied";
        EXPECT_THROW(messaging::ManageGroup(*db,actor,denied,result),messaging::GroupError);
    }
    for (const auto* operation : {"leave","remove","transfer"}) {
        auto owner=add; owner["operation"]=operation; owner["target_uid"]=7;
        EXPECT_THROW(messaging::ManageGroup(*db,7,owner,result),messaging::GroupError);
    }
    auto overflow=add; overflow["members"].clear();
    for (int uid=20;uid<39;++uid) overflow["members"].append(uid);
    EXPECT_THROW(messaging::ManageGroup(*db,7,overflow,result),messaging::GroupError);

    messaging::ManageGroup(*db,7,add,result); EXPECT_EQ(result["group_revision"].asString(),"2");
    auto atomic=add; atomic["request_id"]=Uuid("partial-add"); atomic["expected_revision"]="2";
    atomic["members"].clear(); atomic["members"].append(10); atomic["members"].append(11);
    EXPECT_THROW(messaging::ManageGroup(*db,7,atomic,result),messaging::GroupError);
    EXPECT_THROW(messaging::GroupDirectory(*db,302,10),messaging::GroupError);
    auto stale_version=add; stale_version["request_id"]=Uuid("stale-version");
    EXPECT_THROW(messaging::ManageGroup(*db,7,stale_version,result),messaging::GroupError);
    messaging::ManageGroup(*db,7,add,result); EXPECT_EQ(result["group_revision"].asString(),"2");
    auto conflict=add; conflict["members"][0]=10;
    EXPECT_THROW(messaging::ManageGroup(*db,7,conflict,result),messaging::GroupError);
    Json::Value history; history["membership_epoch"]="1";
    messaging::SyncPage(*db,9,302,0,history,65535); EXPECT_TRUE(history["msgs"].empty());
    auto current=send("after-join"); ASSERT_TRUE(current.IsSuccess());
    history.clear(); messaging::SyncPage(*db,9,302,0,history,65535);
    ASSERT_EQ(history["msgs"].size(),1); EXPECT_EQ(history["msgs"][0]["message_id"].asInt(),current.items[0].message_id);

    Json::Value remove; remove["chat_id"]=302; remove["request_id"]=Uuid("remove-nine");
    remove["expected_revision"]="2"; remove["operation"]="remove"; remove["target_uid"]=9;
    messaging::ManageGroup(*db,7,remove,result);
    EXPECT_THROW(messaging::SyncPage(*db,9,302,0,history,65535),std::exception);
    ASSERT_TRUE(send("while-away").IsSuccess());
    add["request_id"]=Uuid("rejoin-nine"); add["expected_revision"]="3";
    messaging::ManageGroup(*db,7,add,result);
    history.clear(); history["membership_epoch"]="1";
    EXPECT_THROW(messaging::SyncPage(*db,9,302,0,history,65535),std::exception);
    history.clear(); history["membership_epoch"]="2";
    messaging::SyncPage(*db,9,302,0,history,65535); EXPECT_TRUE(history["msgs"].empty());
    message_commit::MySqlMessageCommitAdapter stale(*db); stale.SetGroupEpoch(1);
    EXPECT_FALSE(message_commit::Commit(stale,{9},9,0,302,{{Uuid("stale-epoch"),"stale"}},deadline,true).IsSuccess());

    resource::ResourceCatalog catalog(std::getenv("MESSAGE_SYNC_TEST_MYSQL"),"root","","message_sync_test");
    catalog.Publish("dynamic-resource",7,"file.txt","application/octet-stream",3,std::string(64,'a'));
    const auto resource=catalog.CommitMessage(7,0,302,Uuid("group-resource"),"dynamic-resource",1);
    EXPECT_GT(resource.id,0); EXPECT_TRUE(catalog.CanRead(9,"dynamic-resource")); EXPECT_FALSE(catalog.CanRead(10,"dynamic-resource"));
    EXPECT_EQ(catalog.CommitMessage(7,0,302,Uuid("group-resource"),"dynamic-resource",1).id,resource.id);
    add["members"][0]=10; add["request_id"]=Uuid("add-ten-late"); add["expected_revision"]="4";
    messaging::ManageGroup(*db,7,add,result); EXPECT_FALSE(catalog.CanRead(10,"dynamic-resource"));
    remove["request_id"]=Uuid("remove-nine-again"); remove["expected_revision"]="5";
    messaging::ManageGroup(*db,7,remove,result); EXPECT_FALSE(catalog.CanRead(9,"dynamic-resource"));
    Json::Value transfer; transfer["chat_id"]=302; transfer["request_id"]=Uuid("transfer-eight");
    transfer["expected_revision"]="6"; transfer["operation"]="transfer"; transfer["target_uid"]=8;
    messaging::ManageGroup(*db,7,transfer,result); EXPECT_EQ(result["owner_uid"].asInt(),8);
    auto leave=transfer; leave["operation"]="leave"; leave["request_id"]=Uuid("old-owner-leaves"); leave["expected_revision"]="7";
    messaging::ManageGroup(*db,7,leave,result); EXPECT_EQ(result["group_state"].asString(),"left");
    auto dissolve=leave; dissolve["operation"]="dissolve"; dissolve["request_id"]=Uuid("dissolve"); dissolve["expected_revision"]="8";
    EXPECT_THROW(messaging::ManageGroup(*db,7,dissolve,result),messaging::GroupError);
    messaging::ManageGroup(*db,8,dissolve,result); EXPECT_EQ(result["group_state"].asString(),"dissolved");
    messaging::ManageGroup(*db,8,dissolve,result); EXPECT_EQ(result["group_revision"].asString(),"9");
    EXPECT_FALSE(catalog.CanRead(8,"dynamic-resource"));
    EXPECT_THROW(messaging::SyncPage(*db,8,302,0,history,65535),std::exception);
    messaging::GroupQuery cleanup(*db,"DELETE FROM friend WHERE self_id=7 AND other_id IN (9,10)",{},false);
}

/** 验证两个实例同时修改同一群版本时只有一个事务成功，失败者不覆盖成功结果。 */
TEST(MessageSync, ConcurrentGroupVersionsSerialize) {
    auto db=Connect();
    messaging::GroupQuery chat(*db,"INSERT INTO chat(chat_id,type) VALUES(303,'group')",{},false);
    messaging::GroupQuery group(*db,"INSERT INTO group_chat(chat_id,name,owner_uid,creator_uid,original_name) VALUES(303,'race',7,7,'race')",{},false);
    messaging::GroupQuery member(*db,"INSERT INTO group_chat_member(chat_id,user_id,role) VALUES(303,7,1)",{},false);
    Json::Value first; first["chat_id"]=303; first["request_id"]=Uuid("race-first");
    first["expected_revision"]="1"; first["operation"]="rename"; first["name"]="First";
    auto second=first; second["request_id"]=Uuid("race-second"); second["name"]="Second";
    std::promise<void> start; auto ready=start.get_future().share();
    const auto run=/** @brief 等待共同起点后在独立数据库连接修改同一版本。 */ [ready](Json::Value request) {
        auto connection=Connect(); ready.wait(); Json::Value result;
        try { messaging::ManageGroup(*connection,7,request,result); return std::string("success"); }
        catch (const messaging::GroupError& error) { return std::string(error.what()); }
    };
    auto one=std::async(std::launch::async,run,first);
    auto two=std::async(std::launch::async,run,second);
    start.set_value();
    ASSERT_EQ(one.wait_for(std::chrono::seconds(8)),std::future_status::ready);
    ASSERT_EQ(two.wait_for(std::chrono::seconds(8)),std::future_status::ready);
    const auto left=one.get(),right=two.get();
    EXPECT_TRUE((left=="success" && right=="VersionConflict") || (right=="success" && left=="VersionConflict"));
    EXPECT_EQ(messaging::GroupDirectory(*db,303,7)["group_revision"].asString(),"2");
}

/** 验证加入和移除等待已持有群锁的发送，边界与后续提交权限保持一致。 */
TEST(MessageSync, MembershipChangesWaitForCommittedMessages) {
    auto db=Connect();
    messaging::GroupQuery chat(*db,"INSERT INTO chat(chat_id,type) VALUES(304,'group')",{},false);
    messaging::GroupQuery group(*db,"INSERT INTO group_chat(chat_id,name,owner_uid) VALUES(304,'ordering',7)",{},false);
    messaging::GroupQuery member(*db,"INSERT INTO group_chat_member(chat_id,user_id,role) VALUES(304,7,1),(304,8,0)",{},false);
    messaging::GroupQuery friendship(*db,"INSERT IGNORE INTO friend(self_id,other_id,backname) VALUES(7,9,'')",{},false);
    for (const auto* operation : {"add","remove"}) {
        db->setAutoCommit(false); messaging::LockGroup(*db,304,7);
        messaging::GroupQuery pending(*db,"INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES(304,7,0,'held',0)",{},false);
        Json::Value request; request["chat_id"]=304; request["request_id"]=Uuid(std::string("order-")+operation);
        request["expected_revision"]=std::string(operation)=="add" ? "1" : "2";
        request["operation"]=operation; request["members"].append(9); request["target_uid"]=9;
        auto mutation=std::async(std::launch::async,/** @brief 独立连接等待群消息事务后执行成员变更。 */ [request] {
            auto connection=Connect(); Json::Value result; messaging::ManageGroup(*connection,7,request,result); return result;
        });
        auto observer=Connect(); std::unique_ptr<sql::Statement> probe(observer->createStatement());
        bool blocked=false; const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while (std::chrono::steady_clock::now()<end && !blocked) {
            std::unique_ptr<sql::ResultSet> rows(probe->executeQuery("SELECT COUNT(*) FROM performance_schema.data_lock_waits"));
            blocked=rows->next() && rows->getInt(1)>0; std::this_thread::yield();
        }
        db->commit(); db->setAutoCommit(true);
        ASSERT_EQ(mutation.wait_for(std::chrono::seconds(8)),std::future_status::ready);
        EXPECT_TRUE(blocked); mutation.get();
        Json::Value page;
        if (std::string(operation)=="add") {
            messaging::SyncPage(*db,9,304,0,page,2048); EXPECT_TRUE(page["msgs"].empty());
        } else {
            EXPECT_THROW(messaging::SyncPage(*db,9,304,0,page,2048),std::exception);
            message_commit::MySqlMessageCommitAdapter sender(*db); sender.SetGroupEpoch(1);
            EXPECT_FALSE(message_commit::Commit(sender,{9},9,0,304,{{Uuid("after-remove"),"denied"}},
                std::chrono::steady_clock::now()+std::chrono::seconds(5),true).IsSuccess());
        }
    }
    messaging::GroupQuery cleanup(*db,"DELETE FROM friend WHERE self_id=7 AND other_id=9",{},false);
}

/** 验证转让与目标退出竞争时只有一个成功，始终保留一个有效群主。 */
TEST(MessageSync, TransferAndLeaveCannotRemoveTheOwner) {
    auto db=Connect();
    messaging::GroupQuery chat(*db,"INSERT INTO chat(chat_id,type) VALUES(305,'group')",{},false);
    messaging::GroupQuery group(*db,"INSERT INTO group_chat(chat_id,name,owner_uid) VALUES(305,'owner race',7)",{},false);
    messaging::GroupQuery member(*db,"INSERT INTO group_chat_member(chat_id,user_id,role) VALUES(305,7,1),(305,8,0)",{},false);
    Json::Value transfer; transfer["chat_id"]=305; transfer["request_id"]=Uuid("owner-transfer-race");
    transfer["expected_revision"]="1"; transfer["operation"]="transfer"; transfer["target_uid"]=8;
    auto leave=transfer; leave["request_id"]=Uuid("owner-leave-race"); leave["operation"]="leave";
    std::promise<void> start; auto ready=start.get_future().share();
    const auto run=/** @brief 共同起点在独立连接按认证身份操作。 */ [ready](int actor,Json::Value request) {
        auto connection=Connect(); ready.wait(); Json::Value result;
        try { messaging::ManageGroup(*connection,actor,request,result); return true; }
        catch (const messaging::GroupError&) { return false; }
    };
    auto one=std::async(std::launch::async,run,7,transfer),two=std::async(std::launch::async,run,8,leave);
    start.set_value(); EXPECT_NE(one.get(),two.get());
    messaging::GroupQuery owners(*db,"SELECT COUNT(*) FROM group_chat g JOIN group_chat_member m ON m.chat_id=g.chat_id "
        "WHERE g.chat_id=305 AND m.state='active' AND m.role=1 AND m.user_id=g.owner_uid",{});
    ASSERT_TRUE(owners.rows->next()); EXPECT_EQ(owners.rows->getInt(1),1);
}
