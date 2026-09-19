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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {
std::unique_ptr<sql::Connection> Connect() {
    const auto* endpoint = std::getenv("MESSAGE_SYNC_TEST_MYSQL");
    if (!endpoint) throw std::runtime_error("requires isolated MESSAGE_SYNC_TEST_MYSQL");
    std::unique_ptr<sql::Connection> connection(sql::mysql::get_mysql_driver_instance()->connect(endpoint, "root", ""));
    connection->setSchema("message_sync_test");
    std::unique_ptr<sql::Statement> statement(connection->createStatement());
    statement->execute("SET SESSION innodb_lock_wait_timeout=5");
    return connection;
}
std::string Uuid(const std::string& name) {
    static std::map<std::string, int> identifiers;
    if (!identifiers.count(name)) identifiers[name] = static_cast<int>(identifiers.size()) + 1;
    std::ostringstream value;
    value << "00000000-0000-4000-8000-" << std::setfill('0') << std::setw(12) << identifiers[name];
    return value.str();
}
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

TEST(MessageSync, CursorCannotPassAnUncommittedWriter) {
    auto first = Connect();
    first->setAutoCommit(false);
    messaging::LockConversation(*first, 104, 7, 11);
    std::unique_ptr<sql::Statement> statement(first->createStatement());
    statement->execute("INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES(104,7,11,'held',0)");
    // Production sync uses the same row lock. Observe an actual DB lock wait,
    // rather than inferring ordering from a fixed sleep.
    auto reading = std::async(std::launch::async, [] {
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
