#include "MysqlDao.h"
#include <string>
#include <chrono>
#include "ConfigMgr.h"
#include "LogMgr.h"

SQLConnection::SQLConnection(sql::Connection* connection, int64_t lasttime) 
	: _connection(connection), _last_operator_time(lasttime) {

}

MysqlPool::MysqlPool(const std::string& url, const std::string& user, const std::string& password, const std::string& schema, 
	int poolSize) : _url(url), _user(user), _password(password), _schema(schema), _pool_size(poolSize), _b_stop(false), _fail_count(0) {
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
			int count = 0;
			while (!_b_stop) {
				if (count >= 60) {
					CheckConnection();
					count = 0;
					continue;
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
				count++;
			}
			});

		_check_thread.detach();
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql pool init failed, error={}", e.what());
	}
}

/**
 * @brief 
 * 心跳检测
 * 
 * 枚举所有的连接，如果出现未操作时间大于给定值，则发出一个select 1的sql查询，维持sql连接
 * 如果sql连接已失效，则创建新的连接
 * 这里的锁的精度太大了，如果在进行心跳的时候，有很多的mysql请求，则会出现获取不到锁的情况
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
			SPDLOG_WARN("mysql keepalive failed, error={}", e.what());

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

// 这里进行优化
// 心跳的意义就是保证在一定时间间隔之内，一定要访问一次数据库，保证连接的存活
// 因此我可以在某一时刻，取出当前连接的大小k（可能小于连接池的大小）
// 因为其余的被取出去正在使用的，在这段事件内已经访问过数据库了，因此可以保证心跳
// 接下来需要至少取出k个连接来进行心跳，因为可能在这段时刻这k个都没有被使用，因此需要心跳保活
// 但是可能这段事件中这k个可能有被取出来使用的，那也没关系，因为心跳的意义就是保证被使用
// 只不过可能在这段时间内某一些连接会被心跳再使用一次（那么原本在k个之外的，他们已经满足这段时间被使用了，但是因为在这k个当中
// 可能有的会在心跳期间被拿出去使用，导致后面用完了放进来的会被再心跳一次）
// 
// 但是心跳包的总数量是没有变得，跟直接添加一个全局锁发送的心跳包数量相同
// 但是锁的精度提高了，可以在处理某个连接的心跳的同时，获取其余的连接，而原始情况必须等待所有心跳完成才可以获得
void MysqlPool::CheckConnectionPro() {
	std::size_t target_count;
	{
		std::lock_guard<std::mutex> lock(_que_mutex);
		target_count = _que.size(); // 通过加锁，获取到现在期望进行心跳的连接数
	}

	std::size_t now_count = 0; // 表示当前已经心跳的数量

	// 获取当前时间戳
	auto currentTime = std::chrono::system_clock::now().time_since_epoch();
	// 将时间戳转换为秒
	long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();

	while (now_count < target_count) {
		std::unique_ptr<SQLConnection> connection; // 用于接收现在需要心跳的连接
		{
			std::lock_guard<std::mutex> lock(_que_mutex);
			// 如果池子为空，代表这段时间所有的连接都一定心跳过了，则直接退出
			if (_que.empty()) {
				break;
			}
			connection = std::move(_que.front());
			_que.pop();
		}

		// 判断连接是否还正常
		bool healthy = true;
		// 间隔小于5分钟
		if (timestamp - connection->_last_operator_time >= 300) {
			try {
				// 发送一个心跳包
				std::unique_ptr<sql::Statement> pstmt(connection->_connection->createStatement());
				pstmt->executeQuery("SELECT 1");
				connection->_last_operator_time = timestamp;
			}
			catch (sql::SQLException& e) {
				SPDLOG_WARN("mysql keepalive failed, error={}", e.what());
				// 连接不正常，则记录一下，在心跳完成后，进行重连
				healthy = false;
				_fail_count++;
			}
		}

		// 连接正常，则加锁，将连接还回去
		if (healthy) {
			std::lock_guard<std::mutex> lock(_que_mutex);
			_que.push(std::move(connection));
			_cond.notify_one();
		}

		now_count++;
	}

	// 重连所有失败的连接
	// 因为一次失败了，后面一直重试，可能还是会失败，这里设置一个最大重试次数，如果重试几次之后，还是连接不上，则返回
	int retry = 0;
	while (_fail_count > 0 && retry < MYSQL_MAX_RETRIES) {
		bool success = reconnection(timestamp);
		if (success) {
			_fail_count--;
		}
		else {
			// 达到最大重试次数后，则退出，等待下一次心跳时再进行连接
			retry++;
		}
	}
}

// 重连一个sql连接
bool MysqlPool::reconnection(long long timestamp) {
	try {
		sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

		// 通过驱动程序连接到数据库
		auto* connection = driver->connect(_url, _user, _password);
		connection->setSchema(_schema);

		// 创建一个自定义sql连接
		auto sqlconnection = std::make_unique<SQLConnection>(connection, timestamp);
		// 加锁，将新连接放入
		{
			std::lock_guard<std::mutex> lock(_que_mutex);
			_que.push(std::move(sqlconnection));
		}

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql reconnect failed, error={}", e.what());
		return false;
	}
}

MysqlPool::~MysqlPool() {
	std::lock_guard<std::mutex> lock(_que_mutex);
	close();
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
	if (_b_stop) {
		return;
	}
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

	_pool.reset(new MysqlPool(host + ":" + port, user, password, schema, 8));
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
		SPDLOG_ERROR("mysql GetUser failed, uid={}, error={}, code={}, state={}", uid, e.what(), e.getErrorCode(), e.getSQLState().c_str());
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
		SPDLOG_ERROR("mysql GetUserByName failed, name={}, error={}", name, e.what());
		return nullptr;
	}
}

// 插入申请好友列表
bool MysqlDao::AddFriendApply(const int& from_uid, const int& to_uid, const std::string& description, const std::string& backname) {
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
			->prepareStatement("INSERT INTO apply_friend (from_uid, to_uid, description, backname) values (?, ?, ?, ?) "
			"ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid, description = description, backname = backname"));
		pstmt->setInt(1, from_uid);
		pstmt->setInt(2, to_uid);
		pstmt->setString(3, description);
		pstmt->setString(4, backname);

		// 执行
		int result = pstmt->executeUpdate();

		if (result < 0) {
			return false;
		}

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql AddFriendApply failed, from_uid={}, to_uid={}, description_size={}, backname_size={}, error={}", from_uid, to_uid, description.size(), backname.size(), e.what());
		return false;
	}

	return false;
}

// 获取申请好友列表
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
			"SELECT apply_friend.from_uid as apply_uid, status, name as apply_name, user.description as apply_description, icon as apply_icon, "
			"sex as apply_sex, apply_friend.description as description, backname, apply_friend.to_uid as to_uid " 
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
			info->_apply_uid = res->getInt("apply_uid");
			info->_apply_name = res->getString("apply_name");
			info->_apply_description = res->getString("apply_description");
			info->_apply_icon = res->getString("apply_icon");
			info->_apply_sex = res->getInt("apply_sex");
			info->_status = res->getInt("status");
			info->_to_uid = res->getInt("to_uid");
			info->_description = res->getString("description");
			info->_backname = res->getString("backname");
			applylist.push_back(info);
		}

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql GetApplyFriendList failed, to_uid={}, start={}, limit={}, error={}", to_uid, start, limit, e.what());
		return false;
	}
}

// 更新两个表
bool MysqlDao::AuthFriendApply(int apply_uid, int auth_uid, std::string auth_backname, std::string apply_backname,
	std::string apply_description, std::string auth_description, std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, int& _chat_id) {
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

		// 首先查询一下当前申请是否已经完成，如果没有完成，则通过行级锁进行加锁
		{
			// 准备修改语句
			std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
				prepareStatement("SELECT id FROM apply_friend WHERE (from_uid = ? and to_uid = ?) OR (from_uid = ? and to_uid = ?) FOR UPDATE"));

			pstmt->setInt(1, apply_uid);
			pstmt->setInt(2, auth_uid);

			pstmt->setInt(3, auth_uid);
			pstmt->setInt(4, apply_uid);

			auto res = pstmt->executeQuery();
			if (!res->next()) {
				connection->_connection->rollback();
				return false;
			}
		}

		// 设置申请表状态为已经更新
		{
			// 准备修改语句
			std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
				prepareStatement("UPDATE apply_friend SET status = 1 WHERE (from_uid = ? and to_uid = ?) OR (from_uid = ? and to_uid = ?)"));

			// 进行双向的更新，因为可以互相申请
			pstmt->setInt(1, apply_uid);
			pstmt->setInt(2, auth_uid);

			pstmt->setInt(3, auth_uid);
			pstmt->setInt(4, apply_uid);

			int res = pstmt->executeUpdate();

			// 执行
			if (res <= 0) {
				connection->_connection->rollback();
				return false;
			}
		}
		
		// 往好友表中插入数据
		{
			// 准备修改语句
			std::unique_ptr<sql::PreparedStatement> pstmt1(connection->_connection->
				prepareStatement("INSERT IGNORE INTO friend(self_id, other_id, backname) VALUES (?, ?, ?)"));
			pstmt1->setInt(1, apply_uid);
			pstmt1->setInt(2, auth_uid);
			pstmt1->setString(3, auth_backname);

			// 执行
			if (pstmt1->executeUpdate() != 1) {
				connection->_connection->rollback();
				return false;
			}

			// 准备修改语句
			std::unique_ptr<sql::PreparedStatement> pstmt2(connection->_connection->
				prepareStatement("INSERT IGNORE INTO friend(self_id, other_id, backname) VALUES (?, ?, ?)"));

			pstmt2->setInt(1, auth_uid);
			pstmt2->setInt(2, apply_uid);
			pstmt2->setString(3, apply_backname);

			// 执行
			if (pstmt2->executeUpdate() != 1) {
				connection->_connection->rollback();
				return false;
			}
		}
		
		// 插入会话表
		int chat_id = 0;
		{
			// 准备查询语句
			std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
				prepareStatement("INSERT INTO chat(type) VALUES (?)"));

			pstmt->setString(1, "private");

			pstmt->executeUpdate();
			// 获取刚刚生成的 chat_id
			std::unique_ptr<sql::Statement> stmt(connection->_connection->createStatement());
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
			if (res->next()) {
				chat_id = res->getInt(1);
			}
			else {
				connection->_connection->rollback();
				return false;
			}
		}

		_chat_id = chat_id;

		// 在私聊表中插入数据
		{
			// 准备查询语句
			std::unique_ptr<sql::PreparedStatement> pstmt2(connection->_connection->
				prepareStatement("INSERT INTO private_chat(chat_id, user1_id, user2_id) VALUES(?, ?, ?)"));

			auto min_userid = std::min(apply_uid, auth_uid);
			auto max_userid = std::max(apply_uid, auth_uid);

			// 这里保证同一对人得私聊只插入一次
			pstmt2->setInt(1, chat_id);
			pstmt2->setInt(2, min_userid);
			pstmt2->setInt(3, max_userid);
			// 执行修改
			if (pstmt2->executeUpdate() != 1) {
				_chat_id = 0;
				connection->_connection->rollback();
				return false;
			}
		}

		// 插入聊天信息
		{
			// 申请人发送的申请信息
			if (!apply_description.empty()) {
				// 准备查询语句
				std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
					prepareStatement("INSERT IGNORE INTO chat_message(chat_id, send_id, recv_id, content, status) "
						"VALUES (?, ?, ?, ?, 1)"));

				pstmt->setInt(1, chat_id);
				pstmt->setInt(2, apply_uid);
				pstmt->setInt(3, auth_uid);
				pstmt->setString(4, apply_description);

				int result = pstmt->executeUpdate();
				if (result == 0) {
					_chat_id = 0;
					connection->_connection->rollback();
					return false;
				}

				// 获取刚刚生成的 message_id
				std::unique_ptr<sql::Statement> stmt(connection->_connection->createStatement());
				std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
				int message_id = -1;
				if (res->next()) {
					message_id = res->getInt(1);
				}
				else {
					_chat_id = 0;
					connection->_connection->rollback();
					return false;
				}

				auto chat_msg = std::make_shared<ChatMessage>(message_id, chat_id, apply_uid, auth_uid, apply_description, 1);
				chat_msgs.push_back(chat_msg);
			}

			SPDLOG_DEBUG("auth friend db message, apply_uid={}, auth_uid={}, auth_description_size={}", apply_uid, auth_uid, auth_description.size());
			// 被申请人发送的验证信息
			if (!auth_description.empty()) {
				// 准备查询语句
				std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
					prepareStatement("INSERT IGNORE INTO chat_message(chat_id, send_id, recv_id, content, status) "
						"VALUES (?, ?, ?, ?, 1)"));

				pstmt->setInt(1, chat_id);
				pstmt->setInt(2, auth_uid);
				pstmt->setInt(3, apply_uid);
				pstmt->setString(4, auth_description);

				int result = pstmt->executeUpdate();
				if (result == 0) {
					_chat_id = 0;
					connection->_connection->rollback();
					return false;
				}

				// 获取刚刚生成的 message_id
				std::unique_ptr<sql::Statement> stmt(connection->_connection->createStatement());
				std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
				int message_id = -1;
				if (res->next()) {
					message_id = res->getInt(1);
				}
				else {
					_chat_id = 0;
					connection->_connection->rollback();
					return false;
				}

				auto chat_msg = std::make_shared<ChatMessage>(message_id, chat_id, auth_uid, apply_uid, auth_description, 1);
				chat_msgs.push_back(chat_msg);
			}
		}

		// 无错误，则提交事务
		connection->_connection->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql AuthFriendApply failed, apply_uid={}, auth_uid={}, error={}", apply_uid, auth_uid, e.what());
		// 有错误，则回滚
		connection->_connection->rollback();
		return false;
	}
}

// 查找好友列表
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
			auto other_id = res->getInt("other_id");
			auto backname = res->getString("backname");
			// 找到uid的好友
			auto user_info = GetUser(other_id);
			if (user_info == nullptr) {
				continue;
			}
			// 设置我给uid的备注名
			user_info->_backname = backname;
			friendList.push_back(user_info);
		}

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql GetFriendList failed, uid={}, error={}", uid, e.what());
		return false;
	}
}

// 获取一页的会话列表
bool MysqlDao::GetUserChatList(int uid, int current_chat_id, int page_size,
	std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_chat_id) {

	auto connection = _pool->GetConnection();

	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 准备查询语句
		std::string sql = "WITH all_chat As ( "
			"SELECT chat_id, 'private' AS type, user1_id, user2_id, 'null' AS group_name "
			"FROM private_chat "
			"WHERE (user1_id = ? OR user2_id = ?) AND chat_id > ? "
			"UNION ALL "
			"SELECT gc.chat_id as chat_id, 'group' AS type, 0 AS user1_id, 0 AS user2_id, name as group_name "
			"FROM group_chat as gc JOIN group_chat_member gcm ON gc.chat_id = gcm.chat_id "
			"WHERE (user_id = ?) AND gc.chat_id > ? "
			") "
			"SELECT * FROM all_chat "
			"ORDER BY chat_id "
			"LIMIT ?";

		std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->prepareStatement(sql));
		
		pstmt->setInt(1, uid);
		pstmt->setInt(2, uid);
		pstmt->setInt(3, current_chat_id);
		pstmt->setInt(4, uid);
		pstmt->setInt(5, current_chat_id);
		// 多取一个用于判断是不是已经取完了
		pstmt->setInt(6, page_size + 1);

		std::unique_ptr<sql::ResultSet> result(pstmt->executeQuery());

		std::vector<std::shared_ptr<ChatInfoBase>> temp;
		while (result->next()) {
			auto type = result->getString("type");
			if (type == "private") {
				auto info = std::make_shared<PrivateChatInfo>();
				info->_chat_id = result->getInt("chat_id");
				info->_user1_id = result->getInt("user1_id");
				info->_user2_id = result->getInt("user2_id");
				info->_type = type;
				temp.push_back(info);
			}
			else if (type == "group") {
				auto info = std::make_shared<GroupChatInfo>();
				info->_group_name = result->getString("group_name");
				info->_chat_id = result->getInt("chat_id");
				info->_type = type;
				temp.push_back(info);
			}
		}

		// 如果比page_size大，代表还没有取完
		if (temp.size() > page_size) {
			load_more = true;
			temp.pop_back();
		}

		// 记录当前当前最后的chat_id
		if (!temp.empty()) {
			last_chat_id = temp.back()->_chat_id;
		}

		chat_list = std::move(temp);

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql GetUserChatList failed, uid={}, current_chat_id={}, page_size={}, error={}", uid, current_chat_id, page_size, e.what());
		return false;
	}
}

// 创建私聊
bool MysqlDao::CreatePrivateChat(int user1_id, int user2_id, int& chat_id) {
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

		// 先查看一下，是否已经有过聊天数据了
		{
			// 准备查询语句
			std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
				prepareStatement("SELECT * FROM private_chat WHERE user1_id = ? and user2_id = ?"));

			auto minn = std::min(user1_id, user2_id);
			auto maxn = std::max(user1_id, user2_id);

			pstmt->setInt(1, user1_id);
			pstmt->setInt(2, user2_id);

			std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

			while (res->next()) {
				chat_id = res->getInt("chat_id");
				break;
			}

			if (chat_id != -1) {
				connection->_connection->commit();
				return true;
			}
		}

		// 插入chat表
		{
			std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
				prepareStatement("INSERT INTO chat(type) VALUES (?)"));
			pstmt->setString(1, "private");
			pstmt->executeUpdate();   // 注意这里用 executeUpdate，不是 executeQuery

			// 获取刚刚生成的 chat_id
			std::unique_ptr<sql::Statement> stmt(connection->_connection->createStatement());
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
			if (res->next()) {
				chat_id = res->getInt(1);
			}
			else {
				connection->_connection->rollback();
				return false;
			}
		}
		
		// 插入私聊表
		{
			// 准备查询语句
			std::unique_ptr<sql::PreparedStatement> pstmt2(connection->_connection->
				prepareStatement("INSERT INTO private_chat(chat_id, user1_id, user2_id) VALUES(?, ?, ?)"));

			auto min_userid = std::min(user1_id, user2_id);
			auto max_userid = std::max(user1_id, user2_id);

			// 这里保证同一对人得私聊只插入一次
			pstmt2->setInt(1, chat_id);
			pstmt2->setInt(2, min_userid);
			pstmt2->setInt(3, max_userid);
			// 执行修改
			if (pstmt2->executeUpdate() != 1) {
				connection->_connection->rollback();
				return false;
			}
		}
		
		// 无错误，则提交事务
		connection->_connection->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql CreatePrivateChat failed, user1_id={}, user2_id={}, error={}", user1_id, user2_id, e.what());
		return false;
	}
}

// 插入新的聊天信息
bool MysqlDao::AddChatMessageList(int from_uid, int to_uid, int chat_id, std::vector<std::pair<std::string, std::string>> cache_msgs,
	std::vector<std::shared_ptr<ChatMessage>>& chat_msgs) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		connection->_connection->setAutoCommit(true);
		_pool->returnConnection(std::move(connection));
		});

	try{
		connection->_connection->setAutoCommit(false);

		// 插入所有的数据
		std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->
			prepareStatement("INSERT INTO chat_message(chat_id, send_id, recv_id, content, status) "
				"VALUES(?, ?, ?, ?, 0)"));
				
		for (auto& p : cache_msgs) {
			pstmt->setInt(1, chat_id);
			pstmt->setInt(2, from_uid);
			pstmt->setInt(3, to_uid);
			pstmt->setString(4, p.second);

			pstmt->executeUpdate();

			// 获取刚刚生成的 message_id
			std::unique_ptr<sql::Statement> stmt(connection->_connection->createStatement());
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));

			if  (res->next()) {
				auto message_id = res->getInt(1);
				auto msg = std::make_shared<ChatMessage>(message_id, p.first, chat_id, from_uid, to_uid, p.second, 0);
				chat_msgs.push_back(msg);
			}
			else {
				connection->_connection->rollback();
				chat_msgs.clear();
				return false;
			}
		}

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql AddChatMessageList failed, from_uid={}, to_uid={}, chat_id={}, msg_count={}, error={}", from_uid, to_uid, chat_id, cache_msgs.size(), e.what());
		return false;
	}
}

// 增量加载部分聊天数据
bool MysqlDao::GetChatMessageList(int chat_id, int current_msg_id, int page_size,
	std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id) {
	auto connection = _pool->GetConnection();

	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, &connection]() {
		_pool->returnConnection(std::move(connection));
		});

	try {
		// 准备查询
		std::string sql = "SELECT * FROM chat_message WHERE chat_id = ? and message_id > ? ORDER BY message_id LIMIT ?";
		std::unique_ptr<sql::PreparedStatement> pstmt(connection->_connection->prepareStatement(sql));

		pstmt->setInt(1, chat_id);
		pstmt->setInt(2, current_msg_id);
		pstmt->setInt(3, page_size + 1); // 多查一个

		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		std::vector<std::shared_ptr<ChatMessage>> lists;

		while (res->next()) {
			auto message_id = res->getInt("message_id");
			auto send_id = res->getInt("send_id");
			auto recv_id = res->getInt("recv_id");
			auto content = res->getString("content");
			auto status = res->getInt("status");
			auto created_at = res->getInt64("created_at");
			auto msg = std::make_shared<ChatMessage>(message_id, chat_id, send_id, recv_id, content, status, created_at);
			
			lists.push_back(msg);
		}

		// 如果超过page_size,表示还没有取完，还能够再取
		if (lists.size() > page_size) {
			load_more = true;
			lists.pop_back();
		}

		// 更新当前最后的message_id
		if (lists.size() != 0) {
			last_msg_id = lists.back()->_message_id;
		}

		chat_list = std::move(lists);

		return true;
	}
	catch (sql::SQLException& e) {
		SPDLOG_ERROR("mysql GetChatMessageList failed, chat_id={}, current_msg_id={}, page_size={}, error={}", chat_id, current_msg_id, page_size, e.what());
		return false;
	}
}
