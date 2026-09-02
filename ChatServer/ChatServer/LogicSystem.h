#pragma once
#include "Singleton.h"
#include "LogicDispatcher.h"
#include <functional>
#include <map>
#include "Data.h"
#include <json/value.h>

class CSession;
class CServer;
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> FunCallBack;
class LogicSystem : public Singleton<LogicSystem>, public LogicDispatcher
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	void SetServer(std::shared_ptr<CServer> pserver);
private:
	LogicSystem();
	bool Dispatch(const LogicMessage& message);
	void RegisterCallBacks();

	/**
	 * @brief 
	 * @param  
	 * @param msg_id 
	 * @param msg_data 
	 * 登录请求的处理逻辑
	 */
	void LoginHandler(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data);

	/**
	 * @brief 
	 * @param baseinfo_key 
	 * @param uid 
	 * @param userinfo 
	 * @return 
	 * 从redis中获取用户信息
	 */
	bool GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	/**
	 * @brief 
	 * @param uid_name 
	 * @param value 
	 * 通过uid获取到用户信息，放入到value中
	 */
	void GetUserByUid(std::string uid, Json::Value& value);
	/**
	 * @brief 
	 * @param uid_name 
	 * @param value 
	 * 通过name获取到用户信息，放入到value中
	 */
	void GetUserByName(std::string name, Json::Value& value);
	/**
	 * @brief 
	 * @param uid 
	 * @param applylist 
	 * @return 
	 * 获取好友申请列表
	 */
	bool GetApplyFriendList(int uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist);
	/**
	 * @brief 
	 * @param uid 
	 * @param friend_list 
	 * @return 
	 * 获取用户好友列表
	 */
	bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list);
	/**
	 * @brief 
	 * @param uid 用户的uid
	 * @param next_chat_id 从哪一个chat_id开始查
	 * @param page_size 查page_size个
	 * @param chat_list 返回的列表
	 * @param load_more 是否已经查完
	 * @param new_next_chat_id 下一次开始查的chat_id
	 * @return 
	 * 
	 * 从数据库中获取用户uid的会话列表，从next_chat_id开始查，获取page_size个， 列表返回到chat_list中
	 * load_more代表下一次还能不能查，new_next_chat_id表示下一次从哪一个开始查
	 */
	bool GetUserChatList(int uid, int current_load_id, int page_size,
		std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_load_id);
	/**
	 * @brief 
	 * @param uid 
	 * @param current_load_id 
	 * @param page_size 
	 * @param chat_list 
	 * @param load_more 
	 * @param last_load_id 
	 * @return 
	 * 增量加载部分聊天数据
	 */
	bool GetChatMessageList(int chat_id, int current_msg_id, int page_size,
		std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);
	
	/**
	 * @brief 
	 * @param uid_name 
	 * @return 
	 * 判断某个字符串是不是只包含数字，是则返回true
	 */
	bool IsOnlyDigit(std::string& uid_name);
	// 回调函数集合， 根据msgid来调用不同的回调函数
	std::map<short, FunCallBack> _fun_callbacks;

	// 保存server
	std::shared_ptr<CServer> _p_server;
};
