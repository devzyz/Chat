#include "MysqlDao.h"
#include <string>
#include <chrono>
#include "ConfigMgr.h"

SQLConnection::SQLConnection(sql::Connection* connection, int64_t lasttime) 
	: _connection(connection), _last_operator_time(lasttime) {

}

MysqlPool::MysqlPool(const std::string& url, const std::string& user, const std::string& password, const std::string& schema, 
	int poolSize) : _url(url), _user(user), _password(password), _schema(schema), _pool_size(poolSize), _b_stop(false) {
	try {
		for (int i = 0; i < _pool_size; i++) {
			sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

			// 通过驱动程序连接到数据库
			auto* connection = driver->connect(_url, _user, _password);
			connection->setSchema(_schema);

			// 获取当前时间戳
			auto currentTime = std::chrono::system_clock::now().time_since_epoch();
			// 将时间戳转换为秒
			long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();

			_que.push(std::make_unique<SQLConnection>(connection, timestamp));
		}

		// 心跳检测线程，通过与sql通信的最后时间戳来进行测试
		_check_thread = std::thread([this]() {
			while (!_b_stop) {
				CheckConnection();
				std::this_thread::sleep_for(std::chrono::seconds(60));
			}
			});

		_check_thread.detach();
	}
	catch (sql::SQLException& e) {
		std::cout << "mysql pool init failed, error is " << e.what() << std::endl;
	}
}

/**
 * @brief 
 * 心跳检测
 * 
 * 枚举所有的连接，如果出现未操作时间大于给定值，则发出一个select 1的sql查询，维持sql连接
 * 如果sql连接已失效，则创建新的连接
 */
void MysqlPool::CheckConnection() {
	std::lock_guard<std::mutex> lock(_que_mutex);

	// 获取当前时间戳
	auto currentTime = std::chrono::system_clock::now().time_since_epoch();
	// 将时间戳转换为秒
	long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();

	int poolsize = _que.size();
	for (int i = 0; i < poolsize; i++) {
		auto con = std::move(_que.front());
		_que.pop();

		if (con == nullptr) {
			continue;
		}

		// 每次循环结束，自动执行pusn操作
		Defer defer([this, &con]() {
			_que.push(std::move(con));
			});

		// 间隔小于10分钟
		if (timestamp - con->_last_operator_time < 600) {
			continue;
		}

		try {
			std::unique_ptr<sql::Statement> pstmt(con->_connection->createStatement());
			pstmt->executeQuery("SELECT 1");
			con->_last_operator_time = timestamp;
		}
		catch (sql::SQLException& e) {
			std::cout << "Error keeping connection alive: " << e.what() << std::endl;

			// 创建新连接，替换旧连接
			sql::mysql::MySQL_Driver* driver = sql::mysql::get_driver_instance();
			auto* new_connect = driver->connect(_url, _user, _password);
			new_connect->setSchema(_schema);

			// 对旧连接进行覆盖，最后会执行defer将连接放回队列中
			con->_connection.reset(new_connect);
			con->_last_operator_time = timestamp;
		}
	}
}

MysqlPool::~MysqlPool() {
	std::lock_guard<std::mutex> lock(_que_mutex);
	while (_que.size()) {
		_que.pop();
	}
}

/**
 * @brief 
 * @return
 * 返回一个SQLConnection
 */
std::unique_ptr<SQLConnection> MysqlPool::GetConnection() {
	std::unique_lock<std::mutex> lock(_que_mutex);
	_cond.wait(lock, [this]() {
		if (_b_stop) {
			return true;
		}
		return !_que.empty();
		});

	if (_b_stop) {
		return nullptr;
	}

	auto connection = std::move(_que.front());
	_que.pop();
	return connection;
}

/**
 * @brief 
 * @param connection 
 * 归还SQLConnection
 */
void MysqlPool::returnConnection(std::unique_ptr<SQLConnection> connection) {
	std::lock_guard<std::mutex> lock(_que_mutex);
	if (_b_stop) {
		return;
	}
	_que.push(std::move(connection));
	_cond.notify_one();
}

/**
 * @brief 
 * 连接池关闭，弹出所有的SQLConnection即可，因为其都是unique_ptr管理的
 */
void MysqlPool::close() {
	_b_stop = true;
	_cond.notify_all();
}

MysqlDao::MysqlDao() {
	auto& configMgr = ConfigMgr::GetInstance();
	std::string host = configMgr["Mysql"]["Host"];
	std::string port = configMgr["Mysql"]["Port"];
	std::string user = configMgr["Mysql"]["User"];
	std::string password = configMgr["Mysql"]["Passwd"];
	std::string schema = configMgr["Mysql"]["Schema"];

	_pool.reset(new MysqlPool(host + ":" + port, user, password, schema, 5));
}

MysqlDao::~MysqlDao() {
	_pool->close();
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid) {
	auto connection = _pool->GetConnection();

	if (connection == nullptr) {
		return nullptr;
	}

	Defer defer([this, &connection]() {
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 准备sql语句
		std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
			prepareStatement("SELECT * FROM user WHERE uid = ?"));
		pstmt->setInt(1, uid);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;

		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->pwd = res->getString("pwd");
			user_ptr->email = res->getString("email");
			user_ptr->name = res->getString("name");
			user_ptr->uid = uid;
			break;
		}

		return user_ptr;
	}
	catch (sql::SQLException& e) {
		std::cout << "SQLException : " << e.what() << std::endl;
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}