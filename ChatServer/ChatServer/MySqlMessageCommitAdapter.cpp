#include "MySqlMessageCommitAdapter.h"
#include "../../common/message/PrivateSendAccess.h"
#include "../../common/message/MessagePersistence.h"

#include <limits>
#include <regex>
#include <stdexcept>

#include <jdbc/mysql_driver.h>
#include <jdbc/cppconn/connection.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>

namespace message_commit {
namespace {

/** @brief 检查单调时钟截止时间，超时抛提交错误供事务层映射。 */
void CheckDeadline(Deadline deadline) {
    if (std::chrono::steady_clock::now() >= deadline) { throw Error::DEADLINE_EXCEEDED; }
}

/** @brief 在当前事务锁定私聊双方或群成员所属会话行；无匹配返回 false，SQL 失败抛异常。 */
bool IsMember(sql::Connection& connection, int sender, int recipient, int chat) {
    if (recipient == 0) {
        try { messaging::LockGroup(connection, chat, sender); return true; }
        catch (const messaging::MembershipDenied&) { return false; }
    }
    try { messaging::LockConversation(connection, chat, sender, recipient); return true; }
    catch (const messaging::MembershipDenied&) { return false; }
}

/** @brief 锁定发送者 UUID 对应记录并核对会话、接收者及正文；不一致抛 CONFLICT。 */
Item ReadIdentity(sql::Connection& connection, int sender, int recipient, int chat,
    const std::pair<std::string, std::string>& message, Disposition disposition) {
    std::unique_ptr<sql::PreparedStatement> query(connection.prepareStatement(
        "SELECT message_id,chat_id,recv_id,content,UNIX_TIMESTAMP(created_at) AS created_epoch "
        "FROM chat_message WHERE send_id=? AND client_msg_uuid=? FOR UPDATE"));
    query->setInt(1, sender);
    query->setString(2, message.first);
    std::unique_ptr<sql::ResultSet> result(query->executeQuery());
    if (!result->next()) { throw Error::STORAGE_UNAVAILABLE; }
    if (result->getInt64("chat_id") != chat || result->getInt64("recv_id") != recipient ||
        result->getString("content").asStdString() != message.second) { throw Error::CONFLICT; }
    const auto identifier = result->getUInt64("message_id");
    if (identifier == 0 || identifier > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
        throw Error::STORAGE_UNAVAILABLE;
    }
    return {static_cast<int>(identifier), message.first, disposition, result->getInt64("created_epoch")};
}

}

std::unique_ptr<sql::Connection> ConnectBounded(const std::string& url, const std::string& user,
    const std::string& password, const std::string& schema) {
    // The synchronous classic connector cannot cancel system DNS resolution.
    static const std::regex endpoint("^(tcp://)?(localhost|[0-9.]+|\\[[0-9a-fA-F:]+\\]):[0-9]+$");
    if (!std::regex_match(url, endpoint)) { throw std::invalid_argument("mysql_numeric_endpoint_required"); }
    sql::ConnectOptionsMap options;
    options["hostName"] = sql::SQLString(url);
    options["userName"] = sql::SQLString(user);
    options["password"] = sql::SQLString(password);
    options["OPT_CONNECT_TIMEOUT"] = 2;
    options["OPT_READ_TIMEOUT"] = 2;
    options["OPT_WRITE_TIMEOUT"] = 2;
    options["OPT_RECONNECT"] = false;
    std::unique_ptr<sql::Connection> connection(sql::mysql::get_mysql_driver_instance()->connect(options));
    connection->setSchema(schema);
    std::unique_ptr<sql::Statement> statement(connection->createStatement());
    statement->execute("SET SESSION innodb_lock_wait_timeout=2");
    return connection;
}

Result MySqlMessageCommitAdapter::Commit(int sender, int recipient, int chat, const Batch& batch,
    Deadline deadline) {
    Result result;
    bool has_transaction = false;
    bool was_autocommit = true;
    try {
        CheckDeadline(deadline);
        was_autocommit = _connection.getAutoCommit();
        // Do not adopt an unrelated caller transaction or implicitly commit it.
        if (!was_autocommit) { _is_reusable = false; return {Error::STORAGE_UNAVAILABLE, {}}; }
        _connection.setAutoCommit(false);
        has_transaction = true;
        if (!IsMember(_connection, sender, recipient, chat)) { throw Error::INVALID_MEMBERSHIP; }
        if (recipient == 0 && _group_epoch > 0) {
            try { messaging::LockGroup(_connection, chat, sender, _group_epoch); }
            catch (const messaging::MembershipDenied&) { throw Error::INVALID_MEMBERSHIP; }
        }
        CheckDeadline(deadline);
        std::unique_ptr<sql::PreparedStatement> insert(_connection.prepareStatement(
            "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status,client_msg_uuid) VALUES(?,?,?,?,0,?)"));
        for (const auto& message : batch) {
            CheckDeadline(deadline);
            if (recipient > 0) {
                std::unique_ptr<sql::PreparedStatement> prior(_connection.prepareStatement(
                    "SELECT message_id FROM chat_message WHERE send_id=? AND client_msg_uuid=? FOR UPDATE"));
                prior->setInt(1,sender); prior->setString(2,message.first);
                std::unique_ptr<sql::ResultSet> existing(prior->executeQuery());
                if (existing->next()) {
                    existing.reset();
                    result.items.push_back(ReadIdentity(_connection,sender,recipient,chat,message,Disposition::EXISTING));
                    continue;
                }
                try { messaging::CheckPrivateSend(_connection,chat,_relationship); }
                catch (const messaging::PrivateSendDenied&) { throw Error::INVALID_MEMBERSHIP; }
            }
            insert->setInt(1, chat);
            insert->setInt(2, sender);
            insert->setInt(3, recipient);
            insert->setString(4, message.second);
            insert->setString(5, message.first);
            Disposition disposition = Disposition::CREATED;
            try {
                insert->executeUpdate();
            } catch (const sql::SQLException& error) {
                if (error.getErrorCode() != 1062) { throw; }
                disposition = Disposition::EXISTING;
            }
            CheckDeadline(deadline);
            result.items.push_back(ReadIdentity(_connection, sender, recipient, chat, message, disposition));
        }
        CheckDeadline(deadline);
        if (_commit) { _commit->Commit(_connection); }
        else { _connection.commit(); }
        has_transaction = false;
        // A confirmed COMMIT is authoritative even if its reply consumes the remaining budget.
    } catch (Error error) {
        result = {error, {}};
    } catch (const sql::SQLException& error) {
        const bool timeout = std::chrono::steady_clock::now() >= deadline || error.getErrorCode() == 1205;
        result = {timeout ? Error::DEADLINE_EXCEEDED : Error::STORAGE_UNAVAILABLE, {}};
        _is_reusable = false;
    } catch (const std::exception&) {
        result = {Error::STORAGE_UNAVAILABLE, {}};
        _is_reusable = false;
    }
    try {
        if (has_transaction) { _connection.rollback(); }
        _connection.setAutoCommit(was_autocommit);
    } catch (const sql::SQLException&) {
        // A disconnected session is discarded; its server transaction rolls back on disconnect.
        _is_reusable = false;
        if (result.IsSuccess()) { return result; }
    }
    return result;
}

}
