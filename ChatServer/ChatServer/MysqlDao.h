#pragma once
#include "Const.h"
#include <iostream>
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include <queue>
#include <mutex>
#include "Data.h"
#include <memory>
#include <atomic>
#include <condition_variable>
#include <thread>
#include "MessageCommit.h"

/**
 * @brief 
 * sql的每个连接包含的信息，一个是sql连接指针，另一个是最后操作时间
 * 因为sql长时间不操作，连接可能会被断开，可以通过检查最后操作时间来实现心跳函数，让sql连接保持存活
 */
class SQLConnection {
public:
	SQLConnection(sql::Connection* connection, int64_t lasttime);
	std::unique_ptr<sql::Connection> _connection;
	int64_t _last_operator_time;
};

/**
 * @brief 
 * sql的连接池
 */
class MysqlPool {
public:
	MysqlPool(const std::string& url, const std::string& user, const std::string& password, const std::string& schema,
		int poolSize);
	~MysqlPool();

	std::unique_ptr<SQLConnection> GetConnection(message_commit::Deadline deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2));
	void returnConnection(std::unique_ptr<SQLConnection> connection) noexcept;
	void close();
private:
	// 连接池队列，以及互斥访问连接池的信号
	std::mutex _que_mutex;
	std::queue<std::unique_ptr<SQLConnection>> _que;

	std::atomic<bool> _b_stop;

	// 当连接池为空的时候，需要通过条件变量释放锁并等待
	std::condition_variable _cond;

	// 连接需要的数据
	int _pool_size;
	std::string _url; // 数据库地址
	std::string _user; // 用户名
	std::string _password; // 密码
	std::string _schema; // 分组

    int _live_count = 0;
};

class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();

	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUserByName(const std::string name);
	bool AddFriendApply(const int& from_uid, const int& to_uid, const std::string& description, const std::string& backname);
	// 获取申请添加to_uid为好友的用户列表
	bool  GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit);
	// 更新好友申请表和好友表
	bool AuthFriendApply(int apply_uid, int auth_uid, std::string auth_backname, std::string apply_backname,
		std::string apply_description, std::string auth_description, std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, int& chat_id);
	// 获取用户好友列表
	bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList);
	// 获取一页的会话列表
	bool GetUserChatList(int uid, int current_chat_id, int page_size,
		std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_chat_id);
	// 创建私聊会话
	bool CreatePrivateChat(int user1_id, int user2_id, int& chat_id);
	// 插入from_uid发给to_uid的对话
	message_commit::Result AddChatMessageList(message_commit::AuthenticatedPrincipal principal,
        int from_uid, int to_uid, int chat_id, const message_commit::Batch& cache_msgs,
        std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, message_commit::Deadline deadline);
	// 增量加载部分聊天数据
	bool GetChatMessageList(int chat_id, int current_msg_id, int page_size,
		std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);
private:
	std::unique_ptr<MysqlPool> _pool;
};
