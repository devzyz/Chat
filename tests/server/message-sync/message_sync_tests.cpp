#include "../../../common/message/MessageReceipts.h"
#include "../../../common/message/MessagePersistence.h"
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
