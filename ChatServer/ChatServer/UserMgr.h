#pragma once
#include "Singleton.h"
#include "ChatSessionState.h"
#include <memory>

/**
 * @brief 
 * 管理所有连接到本服务器的session
 */
class UserMgr : public Singleton<UserMgr>
{
	friend class Singleton<UserMgr>;
public:
	~UserMgr();
	std::shared_ptr<ChatSessionState> Sessions() const;
private:
	UserMgr();
	std::shared_ptr<ChatSessionState> _sessions;
};
