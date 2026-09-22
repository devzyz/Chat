#pragma once
#include <cstdint>
#include <json/json.h>
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
#include "../../common/mysql/ConnectionPool.h"

class SQLConnection {
public:
    explicit SQLConnection(std::unique_ptr<sql::Connection> connection = {}) : _connection(std::move(connection)) {}
    std::unique_ptr<sql::Connection> _connection;
};

class MysqlPool {
public:
    MysqlPool(const std::string& url, const std::string& user, const std::string& password,
        const std::string& schema, int pool_size);
    std::unique_ptr<SQLConnection> GetConnection(message_commit::Deadline deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2));
    void returnConnection(std::unique_ptr<SQLConnection> connection) noexcept;
    void close();
private:
    std::unique_ptr<chat_mysql::ConnectionPool<>> _connections;
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
	bool GetChatMessageList(int principal_uid, int chat_id, int current_msg_id, int page_size,
		std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);
    bool SyncChatMessages(int uid, int chat_id, std::int64_t after, Json::Value& response);
private:
	std::unique_ptr<MysqlPool> _pool;
};
