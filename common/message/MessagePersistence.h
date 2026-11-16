#pragma once

#include "../database/Session.h"
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
#include <algorithm>

namespace messaging {
/** @brief 表示会话或成员资格确实不存在；与数据库故障区分，避免永久拒绝可重试消息。 */
class MembershipDenied : public std::runtime_error {
public:
    /** @brief 保存领域拒绝原因；不用于包装 SQL 或网络异常。 */
    explicit MembershipDenied(const char* reason) : std::runtime_error(reason) {}
};
/** @brief 保存当前群成员资格及本次加入后的消息边界。 */
struct GroupAccess {
    std::int64_t epoch = 0;
    std::int64_t boundary = 0;
};
/** @brief 先锁群再检查有效成员，保证所有群写入和资格变更使用相同锁顺序。 */
inline GroupAccess LockGroup(database::Session connection, int chat, int uid, std::int64_t epoch = 0) {
    std::unique_ptr<database::PreparedStatement> group(
        connection.PrepareStatement("SELECT dissolved FROM group_chat WHERE chat_id=? FOR UPDATE"));
    group->SetInt(1, chat);
    std::unique_ptr<database::ResultSet> row(group->ExecuteQuery());
    if (!row->Next() || row->GetBoolean(1))
        throw MembershipDenied("GroupUnavailable");
    row.reset();
    std::unique_ptr<database::PreparedStatement> member(connection.PrepareStatement(
        "SELECT membership_epoch,joined_after_id FROM group_chat_member WHERE chat_id=? AND user_id=? AND state='active'"));
    member->SetInt(1, chat);
    member->SetInt(2, uid);
    row.reset(member->ExecuteQuery());
    if (!row->Next() || (epoch > 0 && epoch != row->GetInt64(1)))
        throw MembershipDenied("MembershipChanged");
    return {row->GetInt64(1), row->GetInt64(2)};
}
/** @brief 借用 JDBC 连接关闭自动提交，显式提交或在析构时回滚并恢复会话状态。 */
class Transaction final {
public:
    /** @brief 借用连接并关闭自动提交，构造后事务必须显式提交或在析构时回滚。 */
    explicit Transaction(database::Session connection) : _connection(connection) {
        if (!_connection.GetAutoCommit())
            throw std::runtime_error("transaction already active");
        _connection.SetAutoCommit(false);
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
            try {
                _connection.Rollback();
                _connection.SetAutoCommit(true);
            } catch (...) {
                try {
                    _connection.Close();
                } catch (...) {
                    // 连接已无法使用，析构期间忽略关闭异常。
                }
            }
        }
    }
    /** @brief 提交事务并恢复自动提交；提交异常向调用方传播，由析构尝试收尾。 */
    void Commit() {
        _connection.Commit();
        _connection.SetAutoCommit(true);
        _finished = true;
    }

private:
    database::Session _connection;
    bool _finished = false;
};

/** @brief 在当前事务中锁定私聊或群聊行并核对参与者，使同会话提交顺序串行化。 */
inline GroupAccess LockConversation(database::Session connection, int chat, int uid, int recipient = 0) {
    std::unique_ptr<database::PreparedStatement> statement(
        connection.PrepareStatement("SELECT user1_id,user2_id FROM private_chat WHERE chat_id=? FOR UPDATE"));
    statement->SetInt(1, chat);
    std::unique_ptr<database::ResultSet> row(statement->ExecuteQuery());
    if (!row->Next()) {
        row.reset();
        if (recipient != 0)
            throw MembershipDenied("conversation unavailable");
        return LockGroup(connection, chat, uid);
    }
    const int first = row->GetInt(1), second = row->GetInt(2);
    if ((first != uid && second != uid) ||
        (recipient > 0 && !((first == uid && second == recipient) || (second == uid && first == recipient))))
        throw MembershipDenied("not a conversation participant");
    return {};
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
/** @brief 实时通知超过帧上限时只保留同步提示，正文由既有增量同步取得。 */
inline std::string BoundedNotification(Json::Value response) {
    auto bytes = CompactJson(response);
    if (bytes.size() <= 2048)
        return bytes;
    response["notify_msgs"] = Json::Value(Json::arrayValue);
    return CompactJson(response);
}

/** @brief 读取指定游标之后的消息增量并形成同步响应。 */
inline void SyncPage(
    database::Session connection,
    int uid,
    int chat,
    std::int64_t after,
    Json::Value& response,
    std::size_t maximum_body) {
    if (after < 0)
        throw std::invalid_argument("invalid message cursor");
    Transaction transaction(connection);
    const auto access = LockConversation(connection, chat, uid);
    if (access.epoch > 0) {
        if (response.isMember("membership_epoch") &&
            response["membership_epoch"].asString() != std::to_string(access.epoch))
            throw std::runtime_error("MembershipChanged");
        response["membership_epoch"] = std::to_string(access.epoch);
    }
    std::unique_ptr<database::PreparedStatement> statement(connection.PrepareStatement(database::QueryText{
        "SELECT m.message_id,m.send_id,m.recv_id,m.content,UNIX_TIMESTAMP(m.created_at) AS sent_at,"
        "COALESCE(m.client_msg_uuid,'') AS client_uuid FROM chat_message m "
        "WHERE m.chat_id=? AND m.message_id>? ORDER BY m.message_id LIMIT 51",
        "SELECT m.message_id,m.send_id,m.recv_id,m.content,EXTRACT(epoch FROM m.created_at)::bigint AS "
        "sent_at,COALESCE(m.client_msg_uuid,'') AS client_uuid FROM chat_message m WHERE m.chat_id=? AND "
        "m.message_id>? ORDER BY m.message_id LIMIT 51"}));
    statement->SetInt(1, chat);
    statement->SetInt64(2, (std::max)(after, access.boundary));
    std::unique_ptr<database::ResultSet> rows(statement->ExecuteQuery());
    response["msgs"] = Json::Value(Json::arrayValue);
    response["next_cursor"] = Json::Int64(after);
    response["load_more"] = false;
    while (rows->Next()) {
        Json::Value message;
        const auto id = rows->GetInt64("message_id");
        message["message_id"] = Json::Int64(id);
        message["send_id"] = rows->GetInt("send_id");
        message["recv_id"] = rows->GetInt("recv_id");
        message["content"] = std::string(rows->GetString("content"));
        message["created_at"] = Json::Int64(rows->GetInt64("sent_at"));
        message["msg_uuid"] = std::string(rows->GetString("client_uuid"));
        Json::Value candidate = response;
        candidate["msgs"].append(message);
        candidate["next_cursor"] = Json::Int64(id);
        if (candidate["msgs"].size() > 50 || CompactJson(candidate).size() > maximum_body) {
            if (response["msgs"].empty())
                throw std::runtime_error("message exceeds synchronization frame limit");
            response["load_more"] = true;
            break;
        }
        response = std::move(candidate);
    }
    rows.reset();
    transaction.Commit();
}
}
