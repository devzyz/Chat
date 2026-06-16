#pragma once
#include "Singleton.h"
#include "MysqlDao.h"
#include "const.h"
#include <iostream>
#include "Data.h"
#include <memory>

class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	// 根据uid查询用户详细信息
	std::shared_ptr<UserInfo> GetUesr(int uid);
	// 根据name查询用户详细信息
	std::shared_ptr<UserInfo> GetUserByName(const std::string name);
	// 往好友申请表中插入数据
	bool AddFriendApply(const int& from_uid, const int& to_uid, const std::string& description, const std::string& backanme);
	// 读取申请添加to_uid为好友的用户信息列表
	bool GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit);
	// 更新好友申请表和好友表
	bool AuthFriendApply(int apply_uid, int auth_uid, std::string auth_backname, std::string apply_backname, 
		std::string apply_description, std::string auth_description, std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, int& chat_id);
	// 获取用户好友列表
	bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList);
	// 获取一页的会话列表
	bool GetUserChatList(int uid, int next_chat_id, int page_size,
		std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& new_next_chat_id);
	// 创建私聊会话
	bool CreatePrivateChat(int user1_id, int user2_id, int& chat_id);
	// 插入from_uid发给to_uid的对话
	bool AddChatMessageList(int from_uid, int to_uid, int chat_id, std::vector<std::pair<std::string, std::string>> cache_msgs,
		std::vector<std::shared_ptr<ChatMessage>>& chat_msgs);
	// 增量加载部分聊天数据
	bool GetChatMessageList(int chat_id, int current_msg_id, int page_size,
		std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);
private:
	MysqlMgr();
	MysqlDao _dao;
};

