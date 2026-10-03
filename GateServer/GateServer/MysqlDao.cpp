#include "MysqlDao.h"
#include "ConfigMgr.h"
#include "../../schema/SchemaContract.h"
#include "../../common/auth/PasswordHash.h"

namespace {
/** @brief 持有账号级 MySQL 咨询锁及独占连接，登录发布结束前阻止并发改密；池须晚于请求析构。 */
class CredentialLease {
public:
    /** @brief 接管池连接；无效连接拒绝认证。 */
    CredentialLease(MysqlConnectionPool& pool, std::unique_ptr<SqlConnection> connection)
        : pool_(pool), connection_(std::move(connection)) {
        if (!connection_) throw std::runtime_error("credential storage unavailable");
    }
    /** @brief 查询账号 UID 后取得跨 Gate 实例共享的锁；邮箱大小写由数据库规则解析。 */
    int Lock(const std::string& email) {
        std::unique_ptr<sql::PreparedStatement> lookup(Db().prepareStatement("SELECT uid FROM user WHERE email" "=?"));
        lookup->setString(1, email);
        std::unique_ptr<sql::ResultSet> user(lookup->executeQuery());
        if (!user->next()) return 0;
        const int uid = user->getInt(1); user.reset(); lookup.reset();
        lock_ = "chat-auth-" + std::to_string(uid);
        std::unique_ptr<sql::PreparedStatement> acquire(Db().prepareStatement("SELECT GET_LOCK(?,1)"));
        acquire->setString(1, lock_);
        std::unique_ptr<sql::ResultSet> result(acquire->executeQuery());
        if (!result->next() || result->getInt(1) != 1) throw std::runtime_error("credential lock unavailable");
        locked_ = true;
        return uid;
    }
    /** @brief 仅在租约存活期间借出独占 SQL 连接。 */
    sql::Connection& Db() { return *connection_->_con; }
    /** @brief 释放账号锁后归还连接；不确定锁状态时销毁连接，不向池泄漏会话锁。 */
    ~CredentialLease() {
        try {
            if (!lock_.empty()) {
                std::unique_ptr<sql::PreparedStatement> release(Db().prepareStatement("SELECT RELEASE_LOCK(?)"));
                release->setString(1, lock_);
                std::unique_ptr<sql::ResultSet> result(release->executeQuery());
                if (!result->next() || (locked_ && result->getInt(1) != 1)) connection_->_con.reset();
            }
        } catch (...) { connection_->_con.reset(); }
        pool_.ReturnConnection(std::move(connection_));
    }
private:
    MysqlConnectionPool& pool_;
    std::unique_ptr<SqlConnection> connection_;
    std::string lock_;
    bool locked_ = false;
};

/** @brief 使用有限连接和读写期限建立 JDBC 连接，禁止自动重连重放写操作。 */
std::unique_ptr<sql::Connection> ConnectGateMysql(const std::string& url, const std::string& user,
    const std::string& password, const std::string& schema) {
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
    return connection;
}
}

MysqlConnectionPool::MysqlConnectionPool(const std::string& url, const std::string& user,
    const std::string& pass, const std::string& schema, int poolsize)
    try : _connections(std::make_unique<chat_mysql::ConnectionPool<>>(poolsize, /** @brief 用有限连接及读写期限创建 Gate 数据库连接。 */ [=] {
        return ConnectGateMysql(url, user, pass, schema);
    })) {
    if (poolsize > 0) {
        auto lease = _connections->Acquire();
        chat_schema::Verify(*lease);
    }
} catch (const sql::SQLException&) {
    throw std::runtime_error("mysql_initialization_failed");
}

std::unique_ptr<SqlConnection> MysqlConnectionPool::GetConnection() {
    auto result = std::make_unique<SqlConnection>();
    result->_con = _connections->Borrow();
    return result->_con ? std::move(result) : nullptr;
}

void MysqlConnectionPool::ReturnConnection(std::unique_ptr<SqlConnection> connection) noexcept {
    if (connection) _connections->Return(std::move(connection->_con));
}

void MysqlConnectionPool::Close() { _connections->Close(); }

MysqlDao::MysqlDao() {
	auto& configmgr  = ConfigMgr::GetInstance();
	const auto& host = configmgr["Mysql"]["Host"];
	const auto& port = configmgr["Mysql"]["Port"];
	const auto& user = configmgr["Mysql"]["User"];
	const auto& password = configmgr["Mysql"]["Password"];
	const auto& schema = configmgr["Mysql"]["Schema"];
	_pool.reset(new MysqlConnectionPool(host + ":" + port, user, password, schema, 8));
}

MysqlDao::~MysqlDao() {
	_pool->Close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
	auto con = _pool->GetConnection();
    Defer release(/** @brief 归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, &con] { _pool->ReturnConnection(std::move(con)); });

	try {
		if (con == nullptr) {
			return -1;
		}

		// 准备调用存储过程
		std::unique_ptr<sql::PreparedStatement> stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
		// 设置输入参数
		stmt->setString(1, name);
		stmt->setString(2, email);
		stmt->setString(3, authentication::HashPassword(pwd));

		stmt->execute();

		/**
		 * 由于PreparedStatement不直接支持注册输出参数，我们需要使用会话变量或其他方法来获取输出参数的值
		 * 如果存储过程设置了会话变量或有其他方式获取输出参数的值，你可以在这里执行SELECT查询来获取它
         * 例如，如果存储过程设置了一个会话变量@result来存储输出结果，可以这样获取：
		 */
		std::unique_ptr<sql::Statement> stmtResult(con->_con->createStatement());
		std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));
		if (res->next()) {
			int result = res->getInt("result");
			SPDLOG_DEBUG("mysql user registration completed, uid={}", result);
			return result;
		}
		return -1;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql RegUser failed, name={}, error={}, code={}, state={}", name, e.what(), e.getErrorCode(), e.getSQLState().c_str());
		return -1;
	}
}

bool MysqlDao::CheckEmail(const std::string& username, const std::string& email) {
	auto con = _pool->GetConnection();

	if (con == nullptr) {
		return false;
	}

	Defer defer(/** @brief 归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, &con]() {
		_pool->ReturnConnection(std::move(con));
		});

	try {
		// 准备查询语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT email FROM user WHERE name = ?"));

		// 绑定参数
		pstmt->setString(1, username);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		
		// 遍历结果集
		while (res->next()) {
			SPDLOG_TRACE("mysql CheckEmail candidate loaded, username={}", username);
			if (email != res->getString("email")) {
				return false;
			}
			return true;
		}

		return false;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql CheckEmail failed, username={}, error={}", username, e.what());
		return false;
	}
}

bool MysqlDao::UpdatePassword(const std::string& username, const std::string& password, const std::string& email,
    const std::function<bool(int)>& revoke) {
	try {
        CredentialLease lease(*_pool, _pool->GetConnection());
        const int uid = lease.Lock(email);
        if (uid <= 0) return false;

        // 锁覆盖 Token 撤销和密码写入；撤销先失败则密码保持原值，SQL 失败则保守地保持已撤销。
        if (!revoke(uid)) return false;
		// 准备查询语句
		std::unique_ptr<sql::PreparedStatement> pstmt(lease.Db().prepareStatement("UPDATE user SET password = ? WHERE name = ? AND email = ? "));

		// 绑定参数
		pstmt->setString(1, authentication::HashPassword(password));
		pstmt->setString(2, username);
        pstmt->setString(3, email);

		// 执行查询
		int updateCount =  pstmt->executeUpdate();

		SPDLOG_DEBUG("mysql password update completed, username={}, updated_rows={}", username, updateCount);
		return updateCount == 1;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql UpdatePassword failed, username={}, error={}", username, e.what());
		return false;
	}
}

bool MysqlDao::CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo) {
	try {
        auto lease = std::make_shared<CredentialLease>(*_pool, _pool->GetConnection());
        if (lease->Lock(email) <= 0) return false;
		std::unique_ptr<sql::PreparedStatement> pstmt(lease->Db().prepareStatement("SELECT * FROM user WHERE email = ?"));

		pstmt->setString(1, email); // 绑定参数

		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::string origin_password = "";

		// 取到密码
		while (res->next()) {
			origin_password = res->getString("password");
			SPDLOG_TRACE("mysql password hash loaded");
			break;
		}

		// 密码错误
		if (!authentication::VerifyPassword(password, origin_password)) {
			return false;
		}

		// 密码正确
		userinfo.name = res->getString("name");
		userinfo.email = email;
		userinfo.pwd = password;
		userinfo.uid = res->getInt("uid");
        res.reset();
        if (!authentication::IsPasswordHash(origin_password)) {
            std::unique_ptr<sql::PreparedStatement> upgrade(lease->Db().prepareStatement(
                "UPDATE user SET password=? WHERE uid=? AND BINARY password=? "));
            upgrade->setString(1, authentication::HashPassword(password));
            upgrade->setInt(2, userinfo.uid);
            upgrade->setString(3, origin_password);
            if (upgrade->executeUpdate() != 1) return false;
        }
        userinfo.credential_lease = std::move(lease);
		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql CheckPassword failed, error={}", e.what());
		return false;
	}
}
