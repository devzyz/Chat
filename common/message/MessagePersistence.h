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
class Transaction final {
public:
    explicit Transaction(sql::Connection& connection) : _connection(connection) {
        if (!_connection.getAutoCommit()) throw std::runtime_error("transaction already active");
        _connection.setAutoCommit(false);
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;
    ~Transaction() {
        if (!_finished) {
            // A broken connection must never return an open transaction to the pool.
            try { _connection.rollback(); _connection.setAutoCommit(true); }
            catch (...) { try { _connection.close(); } catch (...) { /* Already unusable. */ } }
        }
    }
    void Commit() {
        _connection.commit();
        _connection.setAutoCommit(true);
        _finished = true;
    }
private:
    sql::Connection& _connection;
    bool _finished = false;
};

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

inline std::string CompactJson(const Json::Value& value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["emitUTF8"] = true;
    return Json::writeString(writer, value);
}

// response already contains the request envelope. Size the complete serialized
// response against the existing transport limit, not merely a message count.
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
