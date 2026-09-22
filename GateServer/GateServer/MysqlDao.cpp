#include "MysqlDao.h"
#include "ConfigMgr.h"
#include "../../schema/SchemaContract.h"

namespace {
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
    try : _connections(std::make_unique<chat_mysql::ConnectionPool<>>(poolsize, [=] {
        return ConnectGateMysql(url, user, pass, schema);
    })) {
    if (poolsize > 0) {
        auto lease = _connections->Acquire();
        chat_schema::Verify(*lease);
    }
} catch (const sql::SQLException&) {
    throw std::runtime_error("mysql_initialization_failed");
}

std::unique_ptr<SqlConnection> MysqlConnectionPool::getConnection() {
    auto result = std::make_unique<SqlConnection>();
    result->_con = _connections->Borrow();
    return result->_con ? std::move(result) : nullptr;
}

void MysqlConnectionPool::returnConnection(std::unique_ptr<SqlConnection> connection) noexcept {
    if (connection) _connections->Return(std::move(connection->_con));
}

void MysqlConnectionPool::close() { _connections->Close(); }

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
	_pool->close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
	auto con = _pool->getConnection();
    Defer release([this, &con] { _pool->returnConnection(std::move(con)); });

	try {
		if (con == nullptr) {
			return -1;
		}

		// 准备调用存储过程
		std::unique_ptr<sql::PreparedStatement> stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
		// 设置输入参数
		stmt->setString(1, name);
		stmt->setString(2, email);
		stmt->setString(3, pwd);

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
	auto con = _pool->getConnection();

	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		_pool->returnConnection(std::move(con));
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

bool MysqlDao::UpdatePassword(const std::string& username, const std::string& password) {
	auto con = _pool->getConnection();
	if (con == nullptr) {
		return false;
	}
	Defer defer([this, &con]() {
		_pool->returnConnection(std::move(con));
		});

	try {
		// 准备查询语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET password = ? WHERE name = ?"));

		// 绑定参数
		pstmt->setString(1, password);
		pstmt->setString(2, username);

		// 执行查询
		int updateCount =  pstmt->executeUpdate();

		SPDLOG_DEBUG("mysql password update completed, username={}, updated_rows={}", username, updateCount);
		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql UpdatePassword failed, username={}, error={}", username, e.what());
		return false;
	}
}

bool MysqlDao::CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo) {
	auto con = _pool->getConnection();

	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con] {
		_pool->returnConnection(std::move(con));
		});

	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE email = ?"));

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
		if (password != origin_password) {
			return false;
		}

		// 密码正确
		userinfo.name = res->getString("name");
		userinfo.email = email;
		userinfo.pwd = password;
		userinfo.uid = res->getInt("uid");

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql CheckPassword failed, error={}", e.what());
		return false;
	}
}
