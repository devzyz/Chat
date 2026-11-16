#include "../../../ChatServer/ChatServer/PostgresMessageCommitAdapter.h"
#include "../../../ChatServer/ChatServer/MySqlMessageCommitAdapter.h"
#include "../../../GateServer/GateServer/PostgresAccounts.h"
#include "../../../common/resource/ResourceCatalog.h"
#include "../../../common/social/SocialDirectory.h"
#include "../../../common/message/MessageReceipts.h"
#include <cstdlib>
#include <future>
#include <iostream>

namespace {
// 只接受调用方创建的本地测试库，脚本负责销毁整个夹具。
std::string Env(const char* name) {
    const char* value = std::getenv(name);
    if (!value)
        throw std::runtime_error("MissingFixtureSetting");
    return value;
}
// 用独立连接检查事务及真实锁，失败仅报告用例名。
void Require(bool expected, const char* name) {
    if (!expected)
        throw std::runtime_error(name);
}
// 持有验证成功后由 Gate 请求编排器携带的凭据锁。
struct User {
    int uid = 0;
    std::shared_ptr<void> credential_lease;
};

// 用同一业务入口和断言验证两种驱动下的资料、好友、回执及群管理。
void SharedOperations(database::Session db) {
    Json::Value request, response;
    request["expected_revision"] = "1";
    request["name"] = "  Renamed  ";
    request["description"] = std::string("a\0b", 3);
    social::UpdateProfile(db, 1, request, response);
    auto profile = social::Profile(db, 1);
    Require(
        profile["name"].asString() == "Renamed" && profile["description"] == request["description"],
        "ProfileTrimBytes");
    request["operation"] = "apply";
    request["expected_revision"] = "0";
    request["target_uid"] = 2;
    request["backname"] = "friend";
    social::Manage(db, 1, request, response);
    request["expected_revision"] = "1";
    request["target_uid"] = 1;
    request["operation"] = "accept";
    social::Manage(db, 2, request, response);
    auto relation = social::Relation(db, 1, 2);
    const auto chat = relation["chat_id"].asInt();
    Require(chat > 0 && relation["relationship_active"].asBool(), "FriendAccept");
    social::Query message(
        db,
        "SELECT message_id FROM chat_message WHERE chat_id=? AND recv_id=? ORDER BY message_id LIMIT 1",
        {std::to_string(chat), "2"});
    Require(message.rows->Next(), "FriendSystemMessage");
    Json::Value receipt, entry;
    receipt["version"] = 1;
    receipt["request_id"] = "report";
    receipt["chat_id"] = chat;
    entry["message_id"] = message.rows->GetInt(1);
    entry["level"] = "read";
    receipt["items"].append(entry);
    int peer = 0;
    response.clear();
    messaging::ReceiptRequest(db, 2, receipt, true, response, peer);
    Require(peer == 1 && response["items"][0]["level"].asString() == "read", "ReceiptWrite");
    auto first = response["items"][0]["revision"];
    response.clear();
    messaging::ReceiptRequest(db, 2, receipt, true, response, peer);
    Require(response["items"][0]["revision"] == first, "ReceiptReplay");
    social::Query group(
        db,
        "INSERT INTO group_chat(chat_id,name,owner_uid,creator_uid,original_name) VALUES(90,'old',1,1,'old')",
        {},
        false);
    social::Query member(db, "INSERT INTO group_chat_member(chat_id,user_id,role) VALUES(90,1,1),(90,2,0)", {}, false);
    Json::Value rename;
    rename["chat_id"] = 90;
    rename["request_id"] = "12345678-1234-1234-1234-000000000090";
    rename["expected_revision"] = "1";
    rename["operation"] = "rename";
    rename["name"] = "group name";
    messaging::ManageGroup(db, 1, rename, response);
    auto accepted = response;
    messaging::ManageGroup(db, 1, rename, response);
    Require(response == accepted, "GroupReplay");
    std::unique_ptr<message_commit::Store> store;
    if (db.Mysql())
        store = std::make_unique<message_commit::MySqlMessageCommitAdapter>(*db.Mysql());
    else
        store = std::make_unique<message_commit::PostgresMessageCommitAdapter>(*db.Postgres());
    const message_commit::Batch group_text{{"12345678-1234-1234-1234-000000000099", "group text"}};
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto sent = message_commit::Commit(*store, {2}, 2, 0, 90, group_text, end, true, {0, 1});
    Require(sent.IsSuccess() && sent.items.size() == 1, "GroupText");
    social::Query rejoined(
        db,
        "UPDATE group_chat_member SET membership_epoch=2 WHERE chat_id=90 AND user_id=2",
        {},
        false);
    auto stale = message_commit::Commit(*store, {2}, 2, 0, 90, group_text, end, true, {0, 1});
    Require(stale.error == message_commit::Error::INVALID_MEMBERSHIP, "GroupEpoch");
    auto repeated = message_commit::Commit(*store, {2}, 2, 0, 90, group_text, end, true, {0, 2});
    Require(
        repeated.IsSuccess() && repeated.items[0].message_id == sent.items[0].message_id &&
            repeated.items[0].disposition == message_commit::Disposition::EXISTING,
        "GroupIdentity");
}

// 在测试所属实例上运行真实驱动合同。
void Run() {
    if (std::getenv("CHAT_MYSQL_FIXTURE_PORT")) {
        auto mysql = message_commit::ConnectBounded(
            "127.0.0.1:" + Env("CHAT_MYSQL_FIXTURE_PORT"),
            "root",
            "",
            "chat_pg_reference");
        SharedOperations(*mysql);
        return;
    }
    chat_pg::Settings config{
        Env("CHAT_PG_HOST"),
        Env("CHAT_PG_PORT"),
        Env("CHAT_PG_DATABASE"),
        Env("CHAT_PG_USER"),
        Env("CHAT_PG_PASSWORD")};
    Require(config.host == "127.0.0.1" && config.user == "chat_fixture", "InvalidFixtureTarget");
    chat_pg::Connection db(config), second(config);
    SharedOperations(db);
    gate::PostgresAccounts accounts(config);
    const auto uid = accounts.RegUser("NativeAccount", "A@test", "old");
    Require(uid > 0 && accounts.RegUser("nativeaccount ", "B@test", "old") == 0, "AccountDuplicate");
    Require(
        accounts.CheckEmail("NATIVEACCOUNT", "A@test") && !accounts.CheckEmail("NativeAccount", "a@test"),
        "ExactEmail");
    User user;
    Require(accounts.CheckPassword("a@test", "old", user) && user.uid == uid, "Login");
    // 仅撤销已验证账号的测试凭据。
    const auto revoke = [uid](int actual) {
        return actual == uid;
    };
    Require(!accounts.UpdatePassword("NativeAccount", "new", "A@test", revoke), "LoginLease");
    user.credential_lease.reset();
    Require(accounts.UpdatePassword("NativeAccount", "new", "A@test", revoke), "ResetPassword");
    Require(
        !accounts.CheckPassword("A@test", "old", user) && accounts.CheckPassword("A@test", "new", user),
        "ResetLogin");
    user.credential_lease.reset();

    db.Query("INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(81,85,86)");
    // 构造同一消息身份以检验并发提交。
    const auto send = [](chat_pg::Connection& conn) {
        message_commit::PostgresMessageCommitAdapter store(conn);
        const std::string body("Hello\0world", 11);
        return message_commit::Commit(
            store,
            {85},
            85,
            86,
            81,
            {{"00000000-0000-0000-0000-000000000081", body}},
            std::chrono::steady_clock::now() + std::chrono::seconds(3));
    };
    // 用第二个线程独占一个连接，与主线程并发提交。
    auto first = std::async(std::launch::async, [&] {
        return send(db);
    });
    const auto right = send(second);
    const auto left = first.get();
    Require(
        left.IsSuccess() && right.IsSuccess() && left.items.size() == 1 && right.items.size() == 1,
        "ConcurrentSend");
    Require(
        left.items[0].message_id == right.items[0].message_id &&
            left.items[0].disposition != right.items[0].disposition,
        "ConcurrentIdentity");
    // 分别在真实 COMMIT 两侧断开；客户端始终用原 UUID 重试。
    for (bool acknowledge_lost : {false, true}) {
        chat_pg::Connection affected(config);
        // 在原生提交两侧注入故障以区分重试结果。
        message_commit::PostgresMessageCommitAdapter fault(affected, [=](chat_pg::Connection& conn, auto deadline) {
            if (acknowledge_lost)
                conn.Query("COMMIT", {}, deadline);
            conn.Close();
            throw chat_pg::Error("08006");
        });
        const message_commit::Batch batch{
            {acknowledge_lost ? "00000000-0000-0000-0000-000000000092" : "00000000-0000-0000-0000-000000000091",
             "retry"}};
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        const auto unknown = message_commit::Commit(fault, {85}, 85, 86, 81, batch, end);
        Require(
            unknown.error == message_commit::Error::STORAGE_UNAVAILABLE && unknown.items.empty(),
            "CommitUnconfirmed");
        message_commit::PostgresMessageCommitAdapter retry(second);
        const auto recovered = message_commit::Commit(retry, {85}, 85, 86, 81, batch, end);
        Require(
            recovered.IsSuccess() &&
                recovered.items[0].disposition ==
                    (acknowledge_lost ? message_commit::Disposition::EXISTING : message_commit::Disposition::CREATED),
            "CommitRecovery");
    }
    Json::Value page;
    messaging::SyncPage(db, 85, 81, 0, page, 16384);
    Require(
        page["msgs"].size() == 3 && page["msgs"][0]["content"].asString() == std::string("Hello\0world", 11),
        "SyncBytes");

    resource::ResourceCatalog
        catalog(config.host, config.user, config.password, config.database, "postgresql", config.port);
    const std::string id = "00000000-0000-0000-0000-000000000082";
    catalog.Publish(id, 85, "photo.png", "image/png", 32, std::string(64, 'f'));
    const auto attachment = catalog.CommitMessage(85, 86, 81, "00000000-0000-0000-0000-000000000083", id, 0, 1);
    Require(
        attachment.id > 0 && !attachment.existing && catalog.CanRead(86, id) && !catalog.CanRead(87, id),
        "ResourceGrant");
    catalog.SetAvatar(85, id);
    Require(catalog.GetAvatar(85) == id, "Avatar");
}
}

// 执行存储合同并以退出码报告失败。
int main() {
    try {
        Run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
