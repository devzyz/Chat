#pragma once
#include "Singleton.h"
#include <condition_variable>
#include <mutex>
#include "const.h"
#include <queue>
#include <atomic>
#include <functional>
#include <map>
#include "data.h"
#include <json/value.h>

class CSession;
class LogicNode;
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> FunCallBack;
class LogicSystem : public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	void PostMsgToQue(std::shared_ptr<LogicNode> msg);
private:
	LogicSystem();
	void DealMsg();
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
	 * @param uid_name 
	 * @return 
	 * 判断某个字符串是不是只包含数字，是则返回true
	 */
	bool IsOnlyDigit(std::string& uid_name);
	// 工作线程，用于从逻辑队列中取数据进行处理，当没有数据的时候则需要挂起等待，配合条件变量使用
	std::thread _worker_thread;
	std::condition_variable _cond;
	std::mutex _mutex;

	// 逻辑队列
	std::queue<std::shared_ptr<LogicNode>> _msg_que;

	// 当前工作线程是否关闭
	std::atomic<bool> _b_stop;
	
	// 回调函数集合， 根据msgid来调用不同的回调函数
	std::map<short, FunCallBack> _fun_callbacks;
};

