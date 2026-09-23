#pragma once

#include <jdbc/cppconn/connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <json/json.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace messaging {
/** @brief 借用 JDBC 连接关闭自动提交，显式提交或在析构时回滚并恢复会话状态。 */
class Transaction final {
public:
    /** @brief 借用连接并关闭自动提交，构造后事务必须显式提交或在析构时回滚。 */
    explicit Transaction(sql::Connection& connection) : _connection(connection) {
        if (!_connection.getAutoCommit()) throw std::runtime_error("transaction already active");
        _connection.setAutoCommit(false);
    }
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    Transaction(const Transaction&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    Transaction& operator=(const Transaction&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    Transaction(Transaction&&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    Transaction& operator=(Transaction&&) = delete;
    /** @brief 未完成事务时尝试回滚并恢复自动提交；清理失败关闭连接且不向外抛异常。 */
    ~Transaction() {
        if (!_finished) {
            // A broken connection must never return an open transaction to the pool.
            try { _connection.rollback(); _connection.setAutoCommit(true); }
            catch (...) { try { _connection.close(); } catch (...) { /* Already unusable. */ } }
        }
    }
    /** @brief 提交事务并恢复自动提交；提交异常向调用方传播，由析构尝试收尾。 */
    void Commit() {
        _connection.commit();
        _connection.setAutoCommit(true);
        _finished = true;
    }
private:
    sql::Connection& _connection;
    bool _finished = false;
};

/** @brief 在当前事务中锁定私聊行并核对参与者，使同会话提交顺序串行化。 */
inline void LockConversation(sql::Connection& connection, int chat, int uid, int recipient = 0) {
    std::unique_ptr<sql::PreparedStatement> statement(connection.prepareStatement(
        "SELECT user1_id,user2_id FROM private_chat WHERE chat_id=? FOR UPDATE"));
    statement->setInt(1, chat);
    std::unique_ptr<sql::ResultSet> row(statement->executeQuery());
    if (!row->next()) throw std::runtime_error("conversation unavailable");
    const int first = row->getInt(1), second = row->getInt(2);
    if ((first != uid && second != uid)
        || (recipient > 0 && !((first == uid && second == recipient) || (second == uid && first == recipient))))
        throw std::runtime_error("not a conversation participant");
}

/** @brief 将 JSON 值编码为无额外缩进的协议文本。 */
inline std::string CompactJson(const Json::Value& value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["emitUTF8"] = true;
    return Json::writeString(writer, value);
}

// response already contains the request envelope. Size the complete serialized
// response against the existing transport limit, not merely a message count.
/** @brief 读取指定游标之后的消息增量并形成同步响应。 */
inline void SyncPage(sql::Connection& connection, int uid, int chat, std::int64_t after,
                     Json::Value& response, std::size_t maximum_body) {
    if (after < 0) throw std::invalid_argument("invalid message cursor");
    Transaction transaction(connection);
    LockConversation(connection, chat, uid);
    std::unique_ptr<sql::PreparedStatement> statement(connection.prepareStatement(
        "SELECT m.message_id,m.send_id,m.recv_id,m.content,UNIX_TIMESTAMP(m.created_at) AS sent_at,"
        "COALESCE(m.client_msg_uuid,'') AS client_uuid FROM chat_message m "
        "WHERE m.chat_id=? AND m.message_id>? ORDER BY m.message_id LIMIT 51"));
    statement->setInt(1, chat); statement->setInt64(2, after);
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery());
    response["msgs"] = Json::Value(Json::arrayValue);
    response["next_cursor"] = Json::Int64(after);
    response["load_more"] = false;
    while (rows->next()) {
        Json::Value message;
        const auto id = rows->getInt64("message_id");
        message["message_id"] = Json::Int64(id);
        message["send_id"] = rows->getInt("send_id");
        message["recv_id"] = rows->getInt("recv_id");
        message["content"] = std::string(rows->getString("content"));
        message["created_at"] = Json::Int64(rows->getInt64("sent_at"));
        message["msg_uuid"] = std::string(rows->getString("client_uuid"));
        Json::Value candidate = response;
        candidate["msgs"].append(message);
        candidate["next_cursor"] = Json::Int64(id);
        if (candidate["msgs"].size() > 50 || CompactJson(candidate).size() > maximum_body) {
            if (response["msgs"].empty()) throw std::runtime_error("message exceeds synchronization frame limit");
            response["load_more"] = true;
            break;
        }
        response = std::move(candidate);
    }
    rows.reset();
    transaction.Commit();
}
}
