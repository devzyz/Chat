#pragma once
#include "Singleton.h"
#include <unordered_map>
#include <memory>
#include <mutex>

class CSession;
/**
 * @brief 
 * 管理所有连接到本服务器的session
 */
class UserMgr : public Singleton<UserMgr>
{
	friend class Singleton<UserMgr>;
public:
	~UserMgr();
	std::shared_ptr<CSession> GetSession(int uid);
	void SetUserSession(int uid, std::shared_ptr<CSession> sesson);
	void RemoveUserSession(int uid);
private:
	UserMgr();
	std::mutex _session_mutex;
	std::unordered_map<int, std::shared_ptr<CSession>> _uid_to_session;
};

