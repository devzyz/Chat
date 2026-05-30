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
	std::string password = configMgr["Mysql"]["Password"];
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
			user_ptr->_password = res->getString("password");
			user_ptr->_email = res->getString("email");
			user_ptr->_name = res->getString("name");
			user_ptr->_description = res->getString("description");
			user_ptr->_icon = res->getString("icon");
			user_ptr->_sex = res->getInt("sex");
			user_ptr->_uid = uid;
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

std::shared_ptr<UserInfo> MysqlDao::GetUserByName(const std::string name) {
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
			prepareStatement("SELECT * FROM user where name = ?"));
		pstmt->setString(1, name);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;

		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->_password = res->getString("password");
			user_ptr->_email = res->getString("email");
			user_ptr->_name = res->getString("name");
			user_ptr->_description = res->getString("description");
			user_ptr->_icon = res->getString("icon");
			user_ptr->_sex = res->getInt("sex");
			user_ptr->_uid = res->getInt("uid");
			break;
		}

		return user_ptr;
	}
	catch (sql::SQLException& e) {
		std::cout << "SQLException : " << e.what() << std::endl;
		return nullptr;
	}
}

bool MysqlDao::AddFriendApply(const int& from_uid, const int& to_uid) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 准备查询语句
		std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection
			->prepareStatement("INSERT INTO apply_friend (from_uid, to_uid) values (?, ?) "
			"ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid"));
		pstmt->setInt(1, from_uid);
		pstmt->setInt(2, to_uid);

		// 执行
		int result = pstmt->executeUpdate();

		if (result < 0) {
			return false;
		}

		return true;
	}
	catch (sql::SQLException& e) {
		std::cout << "SQLException : " << e.what() << std::endl;
		return false;
	}

	return false;
}

bool MysqlDao::GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 准备查询语句, 其中start与limit是为了配合动态加载，每次从start开始加载limit个
		std::unique_ptr<sql::PreparedStatement> stmt(connection->_connection->prepareStatement(
			"SELECT apply_friend.from_uid as uid, status, name, description, icon, sex " 
			"FROM apply_friend JOIN user ON apply_friend.from_uid = user.uid "
			"WHERE apply_friend.to_uid = ? and apply_friend.id > ? "
			"ORDER BY apply_friend.id ASC "
			"LIMIT ?"));

		stmt->setInt(1, to_uid);
		stmt->setInt(2, start);
		stmt->setInt(3, limit);

		std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
		std::shared_ptr<ApplyInfo> info = nullptr;

		// 遍历结果
		while (res->next()) {
			info.reset(new ApplyInfo());
			info->_uid = res->getInt("uid");
			info->_description = res->getString("description");
			info->_icon = res->getString("icon");
			info->_sex = res->getInt("sex");
			info->_name = res->getString("name");
			info->_status = res->getInt("status");
			applylist.push_back(info);
		}

		return true;
	}
	catch (sql::SQLException& e) {
		std::cout << "SQLExceptioni : " << e.what() << std::endl;
		return false;
	}
}

// 更新两个表
bool MysqlDao::UpdateFriendAuth(int fromuid, int touid, std::string backname) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		connection->_connection->setAutoCommit(true);
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 手动提交事务
		connection->_connection->setAutoCommit(false);

		// 准备修改语句
		std::unique_ptr<sql::PreparedStatement> pstmt1(connection->_connection->
			prepareStatement("UPDATE apply_friend SET status = 1 WHERE from_uid = ? and to_uid = ?"));
		// 为什么要交换，因为插入的时候，fromuid是申请人，验证的时候fromuid是被申请人
		pstmt1->setInt(1, touid);
		pstmt1->setInt(2, fromuid);

		// 执行
		pstmt1->executeUpdate();

		// 准备修改语句
		std::unique_ptr<sql::PreparedStatement> pstmt2(connection->_connection->
			prepareStatement("INSERT IGNORE INTO friend(self_id, friend_id, backname) VALUES (?, ?, ?)"));

		pstmt2->setInt(1, fromuid);
		pstmt2->setInt(2, touid);
		pstmt2->setString(3, backname);

		// 执行
		pstmt2->executeUpdate();

		// 无错误，则提交事务
		connection->_connection->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		std::cout << "SQLException : " << e.what() << std::endl;
		// 有错误，则回滚
		connection->_connection->rollback();
		return false;
	}
}

bool MysqlDao::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 准备查询
		std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
			prepareStatement("SELECT * FROM friend WHERE self_id = ?"));
		pstmt->setInt(1, uid);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		while (res->next()) {
			auto friend_id = res->getInt("friend_id");
			auto user_info = GetUser(friend_id);
			if (user_info == nullptr) {
				continue;
			}
			friendList.push_back(user_info);
		}

		return true;
	}
	catch (sql::SQLException& e) {
		std::cout << "SQLException : " << e.what() << std::endl;
		return false;
	}
}