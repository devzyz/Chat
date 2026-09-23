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

/** 条件不满足时抛出统一合同失败，避免输出数据库敏感信息。 */
void Require(bool condition) { if (!condition) { throw std::runtime_error("contract_failed"); } }
/** 读取指定环境变量，未配置时返回空字符串。 */
std::string Env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
/** 按隔离测试环境连接 MySQL，使用有界连接设置。 */
std::unique_ptr<sql::Connection> Connect() {
    return ConnectBounded("tcp://" + Env("CHAT_MYSQL_HOST") + ":" + Env("CHAT_MYSQL_PORT"),
        Env("CHAT_MYSQL_USER"), Env("CHAT_MYSQL_PASSWORD"), Env("CHAT_MYSQL_DATABASE"));
}
/** 执行一条测试 SQL，由调用方负责数据库和事务归属。 */
void Execute(sql::Connection& connection, const std::string& text) {
    std::unique_ptr<sql::Statement> statement(connection.createStatement());
    statement->execute(text);
}
/** 查询首行首列整数，缺少结果时判定合同失败。 */
std::int64_t Scalar(sql::Connection& connection, const std::string& text) {
    std::unique_ptr<sql::Statement> statement(connection.createStatement());
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery(text));
    Require(rows->next());
    return rows->getInt64(1);
}
/** 以编号生成格式合法且可重复的测试 UUID。 */
std::string Uuid(int suffix) {
    const auto value = std::to_string(suffix);
    return "12345678-1234-4234-8234-" + std::string(12 - value.size(), '0') + value;
}
/** 以固定截止时间提交测试消息批次，可指定发送方、接收方和会话。 */
Result Send(Store& store, const Batch& batch, int sender = 1, int recipient = 2, int chat = 100) {
    return message_commit::Commit(store, {sender}, sender, recipient, chat, batch,
        std::chrono::steady_clock::now() + std::chrono::seconds(8));
}
/** 记录存储层调用次数，用于证明参数拒绝发生在持久化之前。 */
struct RejectStore final : Store {
    int calls = 0;
    /** 记录一次存储调用并返回默认失败结果。 */
    Result Commit(int, int, int, const Batch&, Deadline) override { ++calls; return {}; }
};
/** 验证 UUID、重复项和身份参数在触及存储之前被拒绝。 */
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
/** 借用控制连接在事务提交边界杀死目标连接，注入提交失败。 */
struct KillAtCommit final : TransactionCommit {
    /** 保存控制连接引用，调用者须使其活过故障注入器。 */
    explicit KillAtCommit(sql::Connection& controller) : control(controller) {}
    sql::Connection& control;
    bool reached = false;
    /** 标记提交边界已到达，断开目标连接后尝试提交。 */
    void Commit(sql::Connection& connection) override {
        reached = true;
        const auto id = Scalar(connection, "SELECT CONNECTION_ID()");
        Execute(control, "KILL CONNECTION " + std::to_string(id));
        connection.commit();
    }
};

/** 仅在所属临时数据库中验证提交、幂等、事务回滚和连接失效合同。 */
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
    auto test = /** 执行指定编号的合同，成功后输出可核对的用例标记。 */ [](int number, const std::function<void()>& body) {
        body();
        std::cout << "PASS T10-MSG-" << (number < 10 ? "0" : "") << number << '\n';
    };
    test(1, Validate);
    test(2, /** 验证首次提交创建一条消息并返回持久化标识与时间。 */ [&] {
        auto result = Send(store, {{Uuid(2), "first"}});
        Require(result.IsSuccess() && result.items.size() == 1);
        Require(result.items[0].disposition == Disposition::CREATED && result.items[0].created_at > 0);
        created_id = result.items[0].message_id;
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == 1);
    });
    test(3, /** 验证同一 UUID 与内容重试返回既有消息，不重复插入。 */ [&] {
        auto result = Send(store, {{Uuid(2), "first"}});
        Require(result.IsSuccess() && result.items[0].message_id == created_id);
        Require(result.items[0].disposition == Disposition::EXISTING);
    });
    test(4, /** 验证同一 UUID 携带不同内容被判冲突。 */ [&] { Require(Send(store, {{Uuid(2), "different"}}).error == Error::CONFLICT); });
    test(5, /** 验证已使用的 UUID 不能迁移到另一接收方和会话。 */ [&] { Require(Send(store, {{Uuid(2), "first"}}, 1, 3, 101).error == Error::CONFLICT); });
    test(6, /** 验证批次中后续冲突会回滚先前项，且不返回部分成功。 */ [&] {
        const auto count = Scalar(*connection, "SELECT COUNT(*) FROM chat_message");
        auto result = Send(store, {{Uuid(6), "rolled back"}, {Uuid(2), "conflict"}});
        Require(result.error == Error::CONFLICT && result.items.empty());
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == count);
    });
    test(7, /** 验证批次内重复 UUID 在提交前被拒绝。 */ [&] { Require(Send(store, {{Uuid(7), "a"}, {Uuid(7), "a"}}).error == Error::INVALID_UUID); });
    test(8, /** 验证无效发送方不能提交消息。 */ [&] { Require(Send(store, {{Uuid(8), "a"}}, 0).error == Error::UNAUTHORIZED_SENDER); });
    test(9, /** 验证认证身份与声明发送方不一致时拒绝提交。 */ [&] {
        auto result = message_commit::Commit(store, {1}, 2, 2, 100, {{Uuid(9), "a"}},
            std::chrono::steady_clock::now() + std::chrono::seconds(5));
        Require(result.error == Error::UNAUTHORIZED_SENDER);
    });
    test(10, /** 验证接收方不属于会话时拒绝提交。 */ [&] { Require(Send(store, {{Uuid(10), "a"}}, 1, 3).error == Error::INVALID_MEMBERSHIP); });
    test(11, /** 验证发送方不属于会话时拒绝提交。 */ [&] { Require(Send(store, {{Uuid(11), "a"}}, 3, 2).error == Error::INVALID_MEMBERSHIP); });
    test(12, /** 验证已到截止时间的请求不会写入消息。 */ [&] {
        auto result = message_commit::Commit(store, {1}, 1, 2, 100, {{Uuid(12), "a"}},
            std::chrono::steady_clock::now());
        Require(result.error == Error::DEADLINE_EXCEEDED);
    });
    test(13, /** 验证两个独立连接并发提交同一 UUID 仍只产生一条消息。 */ [&] {
        auto first_connection = Connect();
        auto second_connection = Connect();
        std::mutex mutex;
        std::condition_variable changed;
        int ready = 0;
        bool go = false;
        Result first;
        Result second;
        auto work = /** 等待统一起跑信号后，使用本线程连接提交同一测试消息。 */ [&](sql::Connection& owned, Result& result) {
            { std::unique_lock<std::mutex> lock(mutex); ++ready; changed.notify_all();
                changed.wait(lock, /** 等待协调者发出并发提交信号。 */ [&] { return go; }); }
            MySqlMessageCommitAdapter adapter(owned);
            result = Send(adapter, {{Uuid(13), "concurrent"}});
        };
        std::thread a(work, std::ref(*first_connection), std::ref(first));
        std::thread b(work, std::ref(*second_connection), std::ref(second));
        { std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, /** 等待两个提交线程均到达起跑屏障。 */ [&] { return ready == 2; }); go = true; changed.notify_all(); }
        a.join(); b.join();
        Require(first.IsSuccess() && second.IsSuccess());
        Require(first.items[0].message_id == second.items[0].message_id);
        Require(first.items[0].disposition != second.items[0].disposition);
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='" + Uuid(13) + "'") == 1);
    });
    test(14, /** 验证提交边界断连不返回成功或部分结果，连接不可复用且批次回滚。 */ [&] {
        auto victim = Connect();
        KillAtCommit boundary(*connection);
        MySqlMessageCommitAdapter adapter(*victim, boundary);
        const auto count = Scalar(*connection, "SELECT COUNT(*) FROM chat_message");
        auto result = Send(adapter, {{Uuid(14), "one"}, {Uuid(114), "two"}});
        Require(boundary.reached && !result.IsSuccess() && result.items.empty() && !adapter.IsReusable());
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == count);
    });
    test(15, /** 验证冲突回滚恢复自动提交，原连接可继续处理下一批消息。 */ [&] {
        Require(connection->getAutoCommit());
        Require(Send(store, {{Uuid(2), "conflict"}}).error == Error::CONFLICT);
        Require(connection->getAutoCommit() && store.IsReusable());
        Require(Send(store, {{Uuid(15), "after rollback"}}).IsSuccess());
    });
    test(16, /** 验证过长内容导致批次整体回滚，没有先前项残留。 */ [&] {
        auto owned = Connect();
        MySqlMessageCommitAdapter adapter(*owned);
        const auto count = Scalar(*connection, "SELECT COUNT(*) FROM chat_message");
        auto result = Send(adapter, {{Uuid(16), "first"}, {Uuid(116), std::string(70000, 'x')}});
        Require(!result.IsSuccess() && result.items.empty());
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message") == count);
    });
    test(17, /** 验证旧数据允许多条空 UUID 消息并存。 */ [&] {
        Execute(*connection, "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES "
            "(100,1,2,'legacy-one',0),(100,1,2,'legacy-two',0)");
        Require(Scalar(*connection, "SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid IS NULL") == 2);
    });
    test(18, /** 验证下一批消息返回按提交顺序递增的持久化标识。 */ [&] {
        auto result = Send(store, {{Uuid(18), "next"}, {Uuid(118), "next-two"}});
        Require(result.IsSuccess() && result.items.size() == 2);
        Require(result.items[0].message_id > created_id && result.items[1].message_id > result.items[0].message_id);
    });
    test(19, /** 验证锁等待在有界时间内失败，且不遗留待提交消息。 */ [&] {
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
    test(20, /** 验证已关闭连接返回存储不可用并被判定不能复用。 */ [&] {
        auto closed = Connect();
        closed->close();
        MySqlMessageCommitAdapter adapter(*closed);
        Require(Send(adapter, {{Uuid(20), "closed"}}).error == Error::STORAGE_UNAVAILABLE);
        Require(!adapter.IsReusable());
    });
}
}

/** 选择纯校验或显式集成场景，以脱敏错误和非零退出码报告失败。 */
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
