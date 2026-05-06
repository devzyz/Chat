#include "MysqlDao.h"
#include "ConfigMgr.h"

SqlConnection::SqlConnection(sql::Connection* con, int64_t lasttime) : _con(con), _last_oper_time(lasttime) {

};

/**
 * @brief
 * @param url ip地址
 * @param user 用户名
 * @param pass 密码
 * @param schema 属于那个服务
 * @param poolsize 连接池大小
 *
 * 创建sql连接池，并启动心跳检测线程
 */
MysqlConnectionPool::MysqlConnectionPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolsize) :
	_url(url), _user(user), _pass(pass), _schema(schema), _poolSize(poolsize), _b_stop(false) {
	try {
		for (int i = 0; i < _poolSize; i++) {
			sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
			auto* con = driver->connect(_url, _user, _pass);
			con->setSchema(_schema);
			// 获取当前时间戳
			auto currentTime = std::chrono::system_clock::now().time_since_epoch();
			// 将时间戳转换为秒
			long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
			_pool.push(std::make_unique<SqlConnection>(con, timestamp));
		}

		_check_thread = std::thread([this] {
			while (!_b_stop) {
				checkConnection();
				std::this_thread::sleep_for(std::chrono::seconds(60));
			}
			});
	}
	catch (sql::SQLException& e) {
		std::cout << "mysql pool init failed, error is " << e.what() << std::endl;
	}
}

// 用于保证每个sql连接的活性
void MysqlConnectionPool::checkConnection() {
	std::lock_guard<std::mutex> lock(_mutex);
	int poolsize = _pool.size();
	// 获取当前时间戳
	auto currentTime = std::chrono::system_clock::now().time_since_epoch();
	// 将时间戳转换为秒
	long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();

	for (int i = 0; i < poolsize; i++) {
		auto con = std::move(_pool.front());
		_pool.pop();

		// 每次循环结束，都会自动执行这个函数
		Defer defer([this, &con]() {
			_pool.push(std::move(con));
			});

		if (timestamp - con->_last_oper_time < 600) {
			continue;
		}

		try {
			std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
			stmt->executeQuery("SELECT 1");
			con->_last_oper_time = timestamp;
			std::cout << "execute timer alive query, cur is " << timestamp << std::endl;
		}
		catch (sql::SQLException& e) {
			std::cout << "Error keeping connection alive : " << e.what() << std::endl;
			// 重新创建连接，并替换旧的连接
			sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
			auto* newcon = driver->connect(_url, _user, _pass);
			newcon->setSchema(_schema);
			con->_con.reset(newcon);
			con->_last_oper_time = timestamp;
		}
	}
}

MysqlConnectionPool::~MysqlConnectionPool() {
	std::unique_lock<std::mutex> lock(_mutex);
	while (!_pool.empty()) {
		_pool.pop();
	}
}

std::unique_ptr<SqlConnection> MysqlConnectionPool::getConnection() {
	std::unique_lock<std::mutex> lock(_mutex);
	_cond.wait(lock, [this] {
		if (_b_stop) {
			return true;
		}
		return !_pool.empty();
		});

	if (_b_stop) {
		return nullptr;
	}

	std::unique_ptr<SqlConnection> con(std::move(_pool.front()));
	_pool.pop();
	return con;
}

void MysqlConnectionPool::returnConnection(std::unique_ptr<SqlConnection> con) {
	std::unique_lock<std::mutex> lock(_mutex);
	if (_b_stop) {
		return;
	}
	_pool.push(std::move(con));
	_cond.notify_one();
}

void MysqlConnectionPool::close() {
	_b_stop = true;
	_cond.notify_all();
}

MysqlDao::MysqlDao() {
	auto& configmgr = ConfigMgr::GetInstance();
	const auto& host = configmgr["Mysql"]["Host"];
	const auto& port = configmgr["Mysql"]["Port"];
	const auto& user = configmgr["Mysql"]["User"];
	const auto& passwd = configmgr["Mysql"]["Passwd"];
	const auto& schema = configmgr["Mysql"]["Schema"];
	_pool.reset(new MysqlConnectionPool(host + ":" + port, user, passwd, schema, 5));
}

MysqlDao::~MysqlDao() {
	_pool->close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
	auto con = _pool->getConnection();

	try {
		if (con == nullptr) {
			return false;
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
			std::cout << "Result : " << result << std::endl;
			_pool->returnConnection(std::move(con));
			return result;
		}
		_pool->returnConnection(std::move(con));
		return -1;
	}
	catch (sql::SQLException& e) {
		_pool->returnConnection(std::move(con));
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
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
			std::cout << "Check Email : " << res->getString("email") << std::endl;
			if (email != res->getString("email")) {
				return false;
			}
			return true;
		}

		return false;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
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
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?"));

		// 绑定参数
		pstmt->setString(1, password);
		pstmt->setString(2, username);

		// 执行查询
		int updateCount = pstmt->executeUpdate();

		std::cout << "Updated rows: " << updateCount << std::endl;
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
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
			origin_password = res->getString("pwd");
			std::cout << "Password : " << origin_password << std::endl;
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
		std::cout << "SQLException : " << e.what();
		return false;
	}
}