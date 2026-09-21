#include "MessageCommit.h"
#include "MySqlMessageCommitAdapter.h"
#include "../../../schema/SchemaContract.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <jdbc/cppconn/connection.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/exception.h>

namespace {
using namespace message_commit;

void Require(bool condition) { if (!condition) { throw std::runtime_error("contract_failed"); } }
std::string Env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
std::unique_ptr<sql::Connection> Connect() {
    return ConnectBounded("tcp://" + Env("CHAT_MYSQL_HOST") + ":" + Env("CHAT_MYSQL_PORT"),
        Env("CHAT_MYSQL_USER"), Env("CHAT_MYSQL_PASSWORD"), Env("CHAT_MYSQL_DATABASE"));
}
void Execute(sql::Connection& connection, const std::string& text) {
    std::unique_ptr<sql::Statement> statement(connection.createStatement());
    statement->execute(text);
}
std::int64_t Scalar(sql::Connection& connection, const std::string& text) {
    std::unique_ptr<sql::Statement> statement(connection.createStatement());
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery(text));
    Require(rows->next());
    return rows->getInt64(1);
}
std::string Uuid(int suffix) {
    const auto value = std::to_string(suffix);
    return "12345678-1234-4234-8234-" + std::string(12 - value.size(), '0') + value;
}
Result Send(Store& store, const Batch& batch, int sender = 1, int recipient = 2, int chat = 100) {
    return message_commit::Commit(store, {sender}, sender, recipient, chat, batch,
        std::chrono::steady_clock::now() + std::chrono::seconds(8));
}
struct RejectStore final : Store {
    int calls = 0;
    Result Commit(int, int, int, const Batch&, Deadline) override { ++calls; return {}; }
};
void Validate() {
    Require(IsCanonicalUuid(Uuid(1)));
    Require(!IsCanonicalUuid("12345678-1234-4234-8234-123456789ABC"));
    Require(!IsCanonicalUuid("00000000-0000-0000-0000-000000000000"));
    Require(!IsCanonicalUuid("not-a-uuid"));
    RejectStore store;
    Require(Send(store, {{"bad", "body"}}).error == Error::INVALID_UUID);
    Require(Send(store, {{Uuid(1), "a"}, {Uuid(1), "a"}}).error == Error::INVALID_UUID);
    Require(Send(store, {{Uuid(1), "a"}}, 0).error == Error::UNAUTHORIZED_SENDER);
    Require(store.calls == 0);
}
struct KillAtCommit final : TransactionCommit {
    explicit KillAtCommit(sql::Connection& controller) : control(controller) {}
    sql::Connection& control;
    bool reached = false;
    void Commit(sql::Connection& connection) override {
        reached = true;
        const auto id = Scalar(connection, "SELECT CONNECTION_ID()");
        Execute(control, "KILL CONNECTION " + std::to_string(id));
        connection.commit();
    }
};

void Integration() {
    // Only the owned fixture may invoke this destructive seed operation.
    const auto database = Env("CHAT_MYSQL_DATABASE");
    Require(database.rfind("chat_", 0) == 0 && database.size() > 8 &&
        database.compare(database.size() - 8, 8, "_message") == 0);
    auto connection = Connect();
    chat_schema::Verify(*connection);
    Execute(*connection, "INSERT INTO user(uid,name,email,password,description,icon,sex) VALUES "
        "(1,'message-one','one@example.invalid','synthetic','','',0),"
        "(2,'message-two','two@example.invalid','synthetic','','',0),"
        "(3,'message-three','three@example.invalid','synthetic','','',0)");
    Execute(*connection, "INSERT INTO chat(chat_id,type) VALUES(100,'private'),(101,'private')");
    Execute(*connection, "INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(100,1,2),(101,1,3)");
    MySqlMessageCommitAdapter store(*connection);
    int created_id = 0;
    auto test = [](int number, const std::function<void()>& body) {
        body();
        std::cout << "PASS T10-MSG-" << (number < 10 ? "0" : "") << number << '\n';
    };
    test(1, Validate);
    test(2, [&] {
        auto result = Send(store, {{Uuid(2), "first"}});
        Require(result.IsSuccess() && result.items.size() == 1);
        Require(result.items[0].disposition == Disposition::CREATED && result.items[0].created_at > 0);
        created_id = result.items[0].message_id;
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == 1);
    });
    test(3, [&] {
        auto result = Send(store, {{Uuid(2), "first"}});
        Require(result.IsSuccess() && result.items[0].message_id == created_id);
        Require(result.items[0].disposition == Disposition::EXISTING);
    });
    test(4, [&] { Require(Send(store, {{Uuid(2), "different"}}).error == Error::CONFLICT); });
    test(5, [&] { Require(Send(store, {{Uuid(2), "first"}}, 1, 3, 101).error == Error::CONFLICT); });
    test(6, [&] {
        const auto count = Scalar(*connection, "SELECT COUNT(*) FROM chat_message");
        auto result = Send(store, {{Uuid(6), "rolled back"}, {Uuid(2), "conflict"}});
        Require(result.error == Error::CONFLICT && result.items.empty());
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == count);
    });
    test(7, [&] { Require(Send(store, {{Uuid(7), "a"}, {Uuid(7), "a"}}).error == Error::INVALID_UUID); });
    test(8, [&] { Require(Send(store, {{Uuid(8), "a"}}, 0).error == Error::UNAUTHORIZED_SENDER); });
    test(9, [&] {
        auto result = message_commit::Commit(store, {1}, 2, 2, 100, {{Uuid(9), "a"}},
            std::chrono::steady_clock::now() + std::chrono::seconds(5));
        Require(result.error == Error::UNAUTHORIZED_SENDER);
    });
    test(10, [&] { Require(Send(store, {{Uuid(10), "a"}}, 1, 3).error == Error::INVALID_MEMBERSHIP); });
    test(11, [&] { Require(Send(store, {{Uuid(11), "a"}}, 3, 2).error == Error::INVALID_MEMBERSHIP); });
    test(12, [&] {
        auto result = message_commit::Commit(store, {1}, 1, 2, 100, {{Uuid(12), "a"}},
            std::chrono::steady_clock::now());
        Require(result.error == Error::DEADLINE_EXCEEDED);
    });
    test(13, [&] {
        auto first_connection = Connect();
        auto second_connection = Connect();
        std::mutex mutex;
        std::condition_variable changed;
        int ready = 0;
        bool go = false;
        Result first;
        Result second;
        auto work = [&](sql::Connection& owned, Result& result) {
            { std::unique_lock<std::mutex> lock(mutex); ++ready; changed.notify_all();
                changed.wait(lock, [&] { return go; }); }
            MySqlMessageCommitAdapter adapter(owned);
            result = Send(adapter, {{Uuid(13), "concurrent"}});
        };
        std::thread a(work, std::ref(*first_connection), std::ref(first));
        std::thread b(work, std::ref(*second_connection), std::ref(second));
        { std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return ready == 2; }); go = true; changed.notify_all(); }
        a.join(); b.join();
        Require(first.IsSuccess() && second.IsSuccess());
        Require(first.items[0].message_id == second.items[0].message_id);
        Require(first.items[0].disposition != second.items[0].disposition);
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='" + Uuid(13) + "'") == 1);
    });
    test(14, [&] {
        auto victim = Connect();
        KillAtCommit boundary(*connection);
        MySqlMessageCommitAdapter adapter(*victim, boundary);
        const auto count = Scalar(*connection, "SELECT COUNT(*) FROM chat_message");
        auto result = Send(adapter, {{Uuid(14), "one"}, {Uuid(114), "two"}});
        Require(boundary.reached && !result.IsSuccess() && result.items.empty() && !adapter.IsReusable());
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == count);
    });
    test(15, [&] {
        Require(connection->getAutoCommit());
        Require(Send(store, {{Uuid(2), "conflict"}}).error == Error::CONFLICT);
        Require(connection->getAutoCommit() && store.IsReusable());
        Require(Send(store, {{Uuid(15), "after rollback"}}).IsSuccess());
    });
    test(16, [&] {
        auto owned = Connect();
        MySqlMessageCommitAdapter adapter(*owned);
        const auto count = Scalar(*connection, "SELECT COUNT(*) FROM chat_message");
        auto result = Send(adapter, {{Uuid(16), "first"}, {Uuid(116), std::string(70000, 'x')}});
        Require(!result.IsSuccess() && result.items.empty());
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == count);
    });
    test(17, [&] {
        Execute(*connection, "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES "
            "(100,1,2,'legacy-one',0),(100,1,2,'legacy-two',0)");
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid IS NULL") == 2);
    });
    test(18, [&] {
        auto result = Send(store, {{Uuid(18), "next"}, {Uuid(118), "next-two"}});
        Require(result.IsSuccess() && result.items.size() == 2);
        Require(result.items[0].message_id > created_id && result.items[1].message_id > result.items[0].message_id);
    });
    test(19, [&] {
        auto blocking = Connect();
        blocking->setAutoCommit(false);
        Execute(*blocking, "UPDATE user SET description='locked' WHERE uid=1");
        auto waiting = Connect();
        MySqlMessageCommitAdapter adapter(*waiting);
        const auto before = std::chrono::steady_clock::now();
        auto result = Send(adapter, {{Uuid(19), "blocked"}});
        blocking->rollback(); blocking->setAutoCommit(true);
        Require(!result.IsSuccess());
        Require(std::chrono::steady_clock::now() - before < std::chrono::seconds(6));
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='" + Uuid(19) + "'") == 0);
    });
    test(20, [&] {
        auto closed = Connect();
        closed->close();
        MySqlMessageCommitAdapter adapter(*closed);
        Require(Send(adapter, {{Uuid(20), "closed"}}).error == Error::STORAGE_UNAVAILABLE);
        Require(!adapter.IsReusable());
    });
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "integration") { Integration(); }
        else { Validate(); }
        std::cout << "message_commit_validation_passed\n";
        return 0;
    } catch (const sql::SQLException& error) {
        std::cerr << "message_commit_sql_error_" << error.getErrorCode() << '\n';
        return 1;
    } catch (const std::exception&) {
        std::cerr << "message_commit_test_failed\n";
        return 1;
    }
}
