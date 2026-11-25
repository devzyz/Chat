#pragma once
#include "../../common/postgres/Verify.h"
#include "../../common/database/ConnectionPool.h"
#include "../../common/auth/PasswordHash.h"
#include <functional>

namespace gate {
// 账号存储复用 Gate 的凭据发布租约，独占会话锁须持续到调用方发布 Token。
class PostgresAccounts final {
    using Pool = database::ConnectionPool<chat_pg::Connection>;

    // 通过独占的连接借用保护会话级锁。池与租约共同存活，释放不明时强制断开。
    class CredentialLease final {
    public:
        // 持有池及独占连接，确保账号锁覆盖凭据发布。
        explicit CredentialLease(std::shared_ptr<Pool> pool) : pool_(std::move(pool)), db_(pool_->Acquire()) {}

        // 在锁前定位 UID，随后调用方在锁内重新读取或更新账号。
        int Lock(const std::string& email) {
            auto rows = db_->Query("SELECT uid FROM chat.account WHERE email_key=chat.account_key(decode($1,'hex'))",
                {chat_pg::Hex(email)});
            if (!PQntuples(rows.get())) return 0;
            uid_ = PQgetvalue(rows.get(), 0, 0);
            try {
                db_->Query("SET lock_timeout='1s'");
                db_->Query("SELECT pg_advisory_lock(1128808789,$1::int)", {uid_});
                db_->Query("SET lock_timeout='2s'");
            } catch (...) {
                db_->Close();
                throw;
            }
            return std::stoi(uid_);
        }

        // 在当前借用内访问连接，调用方不可延长引用生命周期。
        chat_pg::Connection& Db() {
            return *db_;
        }

        // 必须确认解锁后才归还连接，否则物理断开。
        ~CredentialLease() {
            if (uid_.empty() || db_->IsClosed()) return;
            try {
                auto rows = db_->Query("SELECT pg_advisory_unlock(1128808789,$1::int)", {uid_});
                if (std::strcmp(PQgetvalue(rows.get(), 0, 0), "t") != 0) db_->Close();
            } catch (...) {
                db_->Close();
            }
        }

    private:
        std::shared_ptr<Pool> pool_;
        Pool::Lease db_;
        std::string uid_;
    };

public:
    // 建立 Gate 原有容量的有界连接池，并在启动阶段验证结构。
    explicit PostgresAccounts(chat_pg::Settings config)
        : pool_(std::make_shared<Pool>(8,
              // 为每次借用建立可独立复位的物理连接。
              [config] {
                  return std::make_unique<chat_pg::Connection>(config);
              })) {
        auto db = pool_->Acquire();
        chat_pg::Verify(*db);
    }

    // 保留正 UID、重复为零、存储失败为负数的注册结果。
    int RegUser(const std::string& name, const std::string& email, const std::string& password) {
        try {
            auto db = pool_->Acquire();
            auto rows = db->Query("SELECT chat.reg_user(decode($1,'hex'),decode($2,'hex'),$3)",
                {chat_pg::Hex(name), chat_pg::Hex(email), authentication::HashPassword(password)});
            return std::stoi(PQgetvalue(rows.get(), 0, 0));
        } catch (const std::exception&) {
            return -1;
        }
    }

    // 源实现按排序规则查用户名，再按原始字节核对邮箱。
    bool CheckEmail(const std::string& name, const std::string& email) {
        try {
            auto db = pool_->Acquire();
            return PQntuples(
                       db->Query("SELECT 1 FROM chat.account WHERE name_key=chat.account_key(decode($1,'hex')) "
                                 "AND email=decode($2,'hex')",
                             {chat_pg::Hex(name), chat_pg::Hex(email)})
                           .get()) == 1;
        } catch (const std::exception&) {
            return false;
        }
    }

    // 在同一把账号锁内先撤销凭据再更新密码。
    bool UpdatePassword(const std::string& name, const std::string& password, const std::string& email,
        const std::function<bool(int)>& revoke) {
        try {
            CredentialLease lease(pool_);
            const int uid = lease.Lock(email);
            if (uid <= 0 || !revoke(uid)) return false;
            return PQntuples(lease.Db()
                           .Query(
                               "UPDATE chat.account SET password=$1 WHERE uid=$2 AND "
                               "name_key=chat.account_key(decode($3,'hex')) AND email_key=chat.account_key(decode($4,'hex')) RETURNING uid",
                               {authentication::HashPassword(password), std::to_string(uid), chat_pg::Hex(name),
                                   chat_pg::Hex(email)})
                           .get()) == 1;
        } catch (const std::exception&) {
            return false;
        }
    }

    // 验证成功将锁的所有权移交给签发路径；旧格式密码按原值条件升级。
    template <class User> bool CheckPassword(const std::string& email, const std::string& password, User& user) {
        try {
            auto lease = std::make_shared<CredentialLease>(pool_);
            const int uid = lease->Lock(email);
            if (uid <= 0) return false;
            auto rows = lease->Db().Query("SELECT password FROM chat.account WHERE uid=$1", {std::to_string(uid)});
            if (PQntuples(rows.get()) != 1) return false;
            const std::string stored = PQgetvalue(rows.get(), 0, 0);
            if (!authentication::VerifyPassword(password, stored)) return false;
            if (!authentication::IsPasswordHash(stored) &&
                PQntuples(lease->Db()
                        .Query("UPDATE chat.account SET password=$1 WHERE uid=$2 AND password=$3 RETURNING uid",
                            {authentication::HashPassword(password), std::to_string(uid), stored})
                        .get()) != 1) {
                return false;
            }
            user.uid = uid;
            user.credential_lease = std::move(lease);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

private:
    std::shared_ptr<Pool> pool_;
};
} // namespace gate
