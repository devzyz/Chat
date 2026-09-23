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

/** @brief 以 unique_ptr 独占一个 JDBC 连接，供连接池转移借用所有权。 */
class SQLConnection {
public:
    /** @brief 初始化SQLConnection，以 unique_ptr 独占一个 JDBC 连接，供连接池转移借用所有权。 */
    explicit SQLConnection(std::unique_ptr<sql::Connection> connection = {}) : _connection(std::move(connection)) {}
    std::unique_ptr<sql::Connection> _connection;
};

/** @brief 包装共享 MySQL 连接池，为聊天事务提供有截止时间的独占借用。 */
class MysqlPool {
public:
    /** @brief 初始化MysqlPool，包装共享 MySQL 连接池，为聊天事务提供有截止时间的独占借用。 */
    MysqlPool(const std::string& url, const std::string& user, const std::string& password,
        const std::string& schema, int pool_size);
    /** @brief 在池的等待策略内借出独占连接；关闭、耗尽或健康检查失败返回空值，使用后须归还。 */
    std::unique_ptr<SQLConnection> GetConnection(message_commit::Deadline deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2));
    /** @brief 接回借出的连接所有权，关闭或不可用连接由底层池丢弃；归还后不得继续访问。 */
    void ReturnConnection(std::unique_ptr<SQLConnection> connection) noexcept;
    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close();
private:
    std::unique_ptr<chat_mysql::ConnectionPool<>> _connections;
};

/** @brief 使用连接池执行聊天数据查询、消息提交和回执事务。 */
class MysqlDao
{
public:
	/** @brief 初始化MysqlDao，以连接池执行用户资料与认证数据操作，失败通过返回值或已声明异常路径传递。 */
	MysqlDao();
	/** @brief 释放数据库访问对象及其持有的连接池。 */
	~MysqlDao();

	/** @brief 按 UID 查询用户资料，未命中或查询失败返回空指针。 */
	std::shared_ptr<UserInfo> GetUser(int uid);
	/** @brief 按用户名查询用户资料，未命中或查询失败返回空指针。 */
	std::shared_ptr<UserInfo> GetUserByName(const std::string name);
	/** @brief 写入双方好友申请与备注资料，返回是否写入成功。 */
	bool AddFriendApply(const int& from_uid, const int& to_uid, const std::string& description, const std::string& backname);
	// 获取申请添加to_uid为好友的用户列表
	/** @brief 分页读取指定用户收到的好友申请并填充输出集合。 */
	bool  GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit);
	// 更新好友申请表和好友表
	/** @brief 审批好友申请并更新双方好友关系和私聊资料，通过输出参数返回会话及消息。 */
	bool AuthFriendApply(int apply_uid, int auth_uid, std::string auth_backname, std::string apply_backname,
		std::string apply_description, std::string auth_description, std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, int& chat_id);
	// 获取用户好友列表
	/** @brief 读取用户好友资料并填充输出集合，失败返回 false。 */
	bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList);
	// 获取一页的会话列表
	/** @brief 读取用户会话分页并通过输出集合、后续页标志及末尾游标返回结果。 */
	bool GetUserChatList(int uid, int current_chat_id, int page_size,
		std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_chat_id);
	// 创建私聊会话
	/** @brief 获取或创建双方私聊会话，通过 chat_id 返回结果；失败返回 false。 */
	bool CreatePrivateChat(int user1_id, int user2_id, int& chat_id);
    /**
     * @brief 校验认证发送者后借用独占连接原子提交文本批次；先清空 chat_msgs，成功按输入顺序填充。
     * @note deadline 覆盖借连接与提交检查；失败返回错误及空输出。成功包含幂等旧消息，不表示送达。
     */
	message_commit::Result AddChatMessageList(message_commit::AuthenticatedPrincipal principal,
        int from_uid, int to_uid, int chat_id, const message_commit::Batch& cache_msgs,
        std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, message_commit::Deadline deadline);
    /**
     * @brief 验证 principal_uid 的会话成员资格，按 ID 升序读取 current_msg_id 之后至多 page_size 条。
     * @note 先清空 chat_list，load_more 置 false，last_msg_id 置输入游标；成功输出下一游标与是否还有记录。
     * 参数非法、无权限、无连接或 SQL 失败返回 false；调用方不得使用失败结果。
     */
	bool GetChatMessageList(int principal_uid, int chat_id, int current_msg_id, int page_size,
		std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);
    /** @brief report 为 true 时上报回执，否则同步；输出响应及对端 UID，失败填充 receipt_error。 */
    bool HandleReceiptRequest(int uid, const Json::Value& request, bool report, Json::Value& response, int& peer);
    /** @brief 借连接按 after 游标同步消息到 response；无连接或异常返回 false，失败响应不可使用。 */
    bool SyncChatMessages(int uid, int chat_id, std::int64_t after, Json::Value& response);
private:
	std::unique_ptr<MysqlPool> _pool;
};
