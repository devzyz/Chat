#pragma once
#include "Singleton.h"
#include <hiredis/hiredis.h>

class DistLock : public Singleton<DistLock>
{
	friend class Singleton<DistLock>;
public:
	~DistLock();
	// »ñÈ¡Ëø
	std::string acquireLock(redisContext* context, const std::string& lockName, int lockTimeout, int acquireTimeout);
	// ÊÍ·ÅËø
	bool releaseLock(redisContext* context, const std::string& lockName, const std::string& identifier);
private:
	DistLock() = default;
};

