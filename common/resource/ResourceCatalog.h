#pragma once
#include "../mysql/ConnectionPool.h"
#include "../message/MessagePersistence.h"
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <json/json.h>

namespace resource {
// Shared data contract for ready resources and committed resource-message references.
// Upload offsets remain local to the single ResourceServer filesystem.
/** @brief 管理已发布资源、头像与资源消息引用的共享数据库合同；上传偏移仍由本机文件存储维护。 */
class ResourceCatalog {
public:
    /** @brief 建立目录数据库连接池，配置有限连接/读写期限并禁止自动重连。 */
    ResourceCatalog(const std::string& host, const std::string& user,
                    const std::string& password, const std::string& schema)
        : _pool(2, /** @brief 以固定连接和 I/O 期限建立目录数据库连接，并禁止自动重连。 */ [=] {
            sql::ConnectOptionsMap options;
            options["hostName"] = host; options["userName"] = user; options["password"] = password;
            options["OPT_CONNECT_TIMEOUT"] = 3; options["OPT_READ_TIMEOUT"] = 5; options["OPT_WRITE_TIMEOUT"] = 5;
            options["OPT_RECONNECT"] = false;
            std::unique_ptr<sql::Connection> connection(sql::mysql::get_mysql_driver_instance()->connect(options));
            connection->setSchema(schema); return connection;
        }) {}
    /** @brief 把已就绪资源元数据登记到共享目录，重复资源 ID 不覆盖既有元数据。 */
    void Publish(const std::string& id, int owner, const std::string& name,
                 const std::string& type, std::uint64_t size, const std::string& digest) {
        auto lease = Acquire();
        std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
            "INSERT INTO resource_file(resource_id, owner_uid, name, media_type, size_bytes, sha256) "
            "VALUES(?,?,?,?,?,?) ON DUPLICATE KEY UPDATE resource_id=resource_id"));
        statement->setString(1, id); statement->setInt(2, owner); statement->setString(3, name);
        statement->setString(4, type); statement->setUInt64(5, size); statement->setString(6, digest);
        statement->executeUpdate();
    }
    /** @brief 检查认证用户能否读取公开头像或其参与私聊引用的资源。 */
    bool CanRead(int uid, const std::string& id) {
        auto lease = Acquire();
        std::unique_ptr<sql::PreparedStatement> avatar(lease->prepareStatement(
            "SELECT 1 FROM user_avatar WHERE resource_id=? LIMIT 1"));
        avatar->setString(1, id);
        std::unique_ptr<sql::ResultSet> profile(avatar->executeQuery());
        if (uid > 0 && profile->next()) return true;
        profile.reset();
        std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
            "SELECT 1 FROM resource_message r JOIN chat_message m ON m.message_id=r.message_id "
            "JOIN private_chat p ON p.chat_id=m.chat_id WHERE r.resource_id=? "
            "AND (m.send_id=? OR m.recv_id=?) AND (p.user1_id=? OR p.user2_id=?) LIMIT 1"));
        statement->setString(1, id);
        for (int index = 2; index <= 5; ++index) statement->setInt(index, uid);
        std::unique_ptr<sql::ResultSet> rows(statement->executeQuery()); return rows->next();
    }
    /** @brief 保存已提交资源消息的服务器 ID 与序列化内容。 */
    struct Message { int id; std::string content; };
    /** @brief 查询用户当前已发布头像的资源 ID，不存在时返回空字符串。 */
    std::string GetAvatar(int uid) {
        auto lease = Acquire();
        std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
            "SELECT resource_id FROM user_avatar WHERE uid=?"));
        statement->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> row(statement->executeQuery());
        return row->next() ? std::string(row->getString(1)) : std::string();
    }
    /** @brief 在 SQL 中核验资源归属及头像限制后更新用户头像；验证不通过抛异常。 */
    void SetAvatar(int uid, const std::string& id) {
        auto lease = Acquire();
        // Published immutable resource metadata is rechecked in the same SQL statement.
        std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
            "INSERT INTO user_avatar(uid,resource_id) SELECT owner_uid,resource_id FROM resource_file "
            "WHERE resource_id=? AND owner_uid=? AND media_type='image/png' AND size_bytes<=1048576 "
            "ON DUPLICATE KEY UPDATE resource_id=VALUES(resource_id)"));
        statement->setString(1, id); statement->setInt(2, uid);
        statement->executeUpdate();
        std::unique_ptr<sql::PreparedStatement> verify(lease->prepareStatement(
            "SELECT resource_id FROM user_avatar WHERE uid=?"));
        verify->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> row(verify->executeQuery());
        if (!row->next() || row->getString(1) != id)
            throw std::runtime_error("avatar resource unavailable or not owned");
    }
    /** @brief 在会话行锁事务中校验资源归属并提交消息引用；同 UUID 幂等复用，身份冲突抛异常。 */
    Message CommitMessage(int sender, int recipient, int chat, const std::string& uuid, const std::string& id) {
        if (uuid.empty() || uuid.size() > 64) throw std::invalid_argument("invalid message UUID");
        auto lease = Acquire();
        messaging::Transaction transaction(*lease);
        {
            // Lock the conversation row to serialize duplicate submissions across Chat instances.
            std::unique_ptr<sql::PreparedStatement> membership(lease->prepareStatement(
                "SELECT chat_id FROM private_chat WHERE chat_id=? AND "
                "((user1_id=? AND user2_id=?) OR (user1_id=? AND user2_id=?)) FOR UPDATE"));
            membership->setInt(1, chat); membership->setInt(2, sender); membership->setInt(3, recipient);
            membership->setInt(4, recipient); membership->setInt(5, sender);
            std::unique_ptr<sql::ResultSet> member(membership->executeQuery());
            if (!member->next()) throw std::runtime_error("not a conversation participant");
            member.reset();
            std::unique_ptr<sql::PreparedStatement> existing(lease->prepareStatement(
                "SELECT m.message_id,m.content,r.resource_id,m.chat_id,m.recv_id FROM resource_message r "
                "JOIN chat_message m ON m.message_id=r.message_id WHERE r.sender_uid=? AND r.client_uuid=?"));
            existing->setInt(1, sender); existing->setString(2, uuid);
            std::unique_ptr<sql::ResultSet> prior(existing->executeQuery());
            if (prior->next()) {
                if (prior->getString(3) != id || prior->getInt(4) != chat || prior->getInt(5) != recipient)
                    throw std::runtime_error("message UUID conflict");
                Message message{prior->getInt(1), prior->getString(2)};
                transaction.Commit(); return message;
            }
            prior.reset();
            std::unique_ptr<sql::PreparedStatement> resource(lease->prepareStatement(
                "SELECT name,media_type,size_bytes,sha256 FROM resource_file WHERE resource_id=? AND owner_uid=?"));
            resource->setString(1, id); resource->setInt(2, sender);
            std::unique_ptr<sql::ResultSet> row(resource->executeQuery());
            if (!row->next()) throw std::runtime_error("resource unavailable or not owned");
            Json::Value descriptor; descriptor["resource_id"] = id;
            descriptor["name"] = std::string(row->getString(1)); descriptor["media_type"] = std::string(row->getString(2));
            descriptor["size"] = std::to_string(row->getUInt64(3)); descriptor["sha256"] = std::string(row->getString(4));
            Json::StreamWriterBuilder writer; writer["indentation"] = "";
            const auto content = std::string("@resource:v1:") + Json::writeString(writer, descriptor);
            std::unique_ptr<sql::PreparedStatement> insert(lease->prepareStatement(
                "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status,client_msg_uuid) VALUES(?,?,?,?,0,?)"));
            insert->setInt(1, chat); insert->setInt(2, sender); insert->setInt(3, recipient); insert->setString(4, content); insert->setString(5, uuid);
            insert->executeUpdate();
            std::unique_ptr<sql::Statement> query(lease->createStatement());
            std::unique_ptr<sql::ResultSet> generated(query->executeQuery("SELECT LAST_INSERT_ID()"));
            if (!generated->next()) throw std::runtime_error("missing message id");
            const auto message_id = generated->getInt(1);
            std::unique_ptr<sql::PreparedStatement> reference(lease->prepareStatement(
                "INSERT INTO resource_message(message_id,resource_id,sender_uid,client_uuid) VALUES(?,?,?,?)"));
            reference->setInt(1, message_id); reference->setString(2, id); reference->setInt(3, sender); reference->setString(4, uuid);
            reference->executeUpdate(); transaction.Commit();
            return {message_id, content};
        }
    }
private:
    /** @brief 借用目录数据库连接并以 RAII 租约保证归还，失败抛异常。 */
    chat_mysql::ConnectionPool<>::Lease Acquire() { return _pool.Acquire(); }
    chat_mysql::ConnectionPool<> _pool;
};
}
