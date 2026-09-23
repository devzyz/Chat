#pragma once
#include <cstdint>
#include <json/json.h>
#include "Singleton.h"
#include "MysqlDao.h"
#include "Const.h"
#include <iostream>
#include "Data.h"
#include <memory>

/** @brief 向聊天业务提供用户、好友和消息持久化操作，具体访问交由 MysqlDao。 */
class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	/** @brief 释放数据库访问对象及其持有的连接池。 */
	~MysqlMgr();
	/** @brief 按 UID 查询用户；不存在或存储不可用时返回 nullptr。 */
	std::shared_ptr<UserInfo> GetUserByUid(int uid);
	// 根据name查询用户详细信息
	/** @brief 按用户名查询用户资料，未命中或查询失败返回空指针。 */
	std::shared_ptr<UserInfo> GetUserByName(const std::string name);
	/** @brief 保存好友申请及备注名，失败返回 false。 */
	bool AddFriendApply(const int& from_uid, const int& to_uid, const std::string& description, const std::string& backname);
	// 读取申请添加to_uid为好友的用户信息列表
	/** @brief 分页读取指定用户收到的好友申请并填充输出集合。 */
	bool GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit);
	// 更新好友申请表和好友表
	/** @brief 审批好友申请并更新双方好友关系和私聊资料，通过输出参数返回会话及消息。 */
	bool AuthFriendApply(int apply_uid, int auth_uid, std::string auth_backname, std::string apply_backname, 
		std::string apply_description, std::string auth_description, std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, int& chat_id);
	// 获取用户好友列表
	/** @brief 读取用户好友资料并填充输出集合，失败返回 false。 */
	bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList);
	// 获取一页的会话列表
	/** @brief 读取用户会话分页并通过输出集合、后续页标志及末尾游标返回结果。 */
	bool GetUserChatList(int uid, int next_chat_id, int page_size,
		std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& new_next_chat_id);
	// 创建私聊会话
	/** @brief 获取或创建双方私聊会话，通过 chat_id 返回结果；失败返回 false。 */
	bool CreatePrivateChat(int user1_id, int user2_id, int& chat_id);
    /** @brief 转交已认证文本批次提交；DAO 先清空 chat_msgs，成功按输入顺序填充，错误不代表可换 UUID 重试。 */
	message_commit::Result AddChatMessageList(message_commit::AuthenticatedPrincipal principal,
        int from_uid, int to_uid, int chat_id, const message_commit::Batch& cache_msgs,
        std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, message_commit::Deadline deadline);
    /** @brief 转交带成员校验的 ID 升序分页，输出消息、后续页标志和末尾 ID；false 时不得消费输出。 */
	bool GetChatMessageList(int principal_uid, int chat_id, int current_msg_id, int page_size,
		std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);
    /** @brief 上报或同步消息回执，输出响应及对端 UID；失败返回 false 并填充 receipt_error。 */
    bool HandleReceiptRequest(int uid, const Json::Value& request, bool report, Json::Value& response, int& peer);
    /** @brief 转交持久化消息增量同步；成功填充 response，false 表示存储或请求处理失败。 */
    bool SyncChatMessages(int uid, int chat_id, std::int64_t after, Json::Value& response);
private:
	/** @brief 初始化MysqlMgr，提供进程内 DAO 访问入口并转发数据库业务操作，不代表跨服务事务。 */
	MysqlMgr();
	MysqlDao _dao;
};
