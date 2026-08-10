#pragma once
#include "Singleton.h"
#include <hiredis/hiredis.h>

class DistLock : public Singleton<DistLock>
{
	friend class Singleton<DistLock>;
public:
	~DistLock();
	// 获取锁
	std::string acquireLock(redisContext* context, const std::string& lockName, int lockTimeout, int acquireTimeout);
	// 释放锁
	bool releaseLock(redisContext* context, const std::string& lockName, const std::string& identifier);
private:
	DistLock() = default;
};

