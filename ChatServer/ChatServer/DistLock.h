#pragma once
#include "Singleton.h"
#include <hiredis/hiredis.h>

/** @brief 通过 Redis 所有者标识和有限租约获取或释放分布式锁，不拥有传入的连接。 */
class DistLock : public Singleton<DistLock>
{
	friend class Singleton<DistLock>;
public:
	/** @brief 结束对象生命周期并释放其成员持有的配置或共享运行资源。 */
	~DistLock();
	// 获取锁
	/** @brief 在有限等待内尝试 SET NX EX，成功返回唯一所有者标识，失败返回空字符串；时间参数为秒。 */
	std::string AcquireLock(redisContext* context, const std::string& lockName, int lockTimeout, int acquireTimeout);
	// 释放锁
	/** @brief 仅当锁值仍等于给定所有者标识时原子删除锁，返回是否释放成功。 */
	bool ReleaseLock(redisContext* context, const std::string& lockName, const std::string& identifier);
private:
	/** @brief 构造无连接所有权的分布式锁操作对象。 */
	DistLock() = default;
};

