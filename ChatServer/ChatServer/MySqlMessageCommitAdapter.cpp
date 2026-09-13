#include "MySqlMessageCommitAdapter.h"

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

void CheckDeadline(Deadline deadline) {
    if (std::chrono::steady_clock::now() >= deadline) { throw Error::DEADLINE_EXCEEDED; }
}

bool IsMember(sql::Connection& connection, int sender, int recipient, int chat) {
    std::unique_ptr<sql::PreparedStatement> query(connection.prepareStatement(
        "SELECT c.chat_id FROM chat c JOIN private_chat p ON p.chat_id=c.chat_id "
        "JOIN user s ON s.uid=? JOIN user r ON r.uid=? "
        "WHERE c.chat_id=? AND c.type='private' AND "
        "((p.user1_id=s.uid AND p.user2_id=r.uid) OR (p.user1_id=r.uid AND p.user2_id=s.uid)) FOR SHARE"));
    query->setInt(1, sender);
    query->setInt(2, recipient);
    query->setInt(3, chat);
    std::unique_ptr<sql::ResultSet> result(query->executeQuery());
    return result->next();
}

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
        CheckDeadline(deadline);
        std::unique_ptr<sql::PreparedStatement> insert(_connection.prepareStatement(
            "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status,client_msg_uuid) VALUES(?,?,?,?,0,?)"));
        for (const auto& message : batch) {
            CheckDeadline(deadline);
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
