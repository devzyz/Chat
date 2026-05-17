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

	void LoginHandler(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data);

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

	// 缓存用户的信息
	std::unordered_map<int, std::shared_ptr<UserInfo>> _users;
};

