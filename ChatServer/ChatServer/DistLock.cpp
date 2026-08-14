#include "DistLock.h"
#include <boost/uuid.hpp>
#include <thread>

DistLock::~DistLock() {

}

std::string generateUUID() {
	boost::uuids::uuid uuid = boost::uuids::random_generator()();
	return to_string(uuid);
}

// 尝试获取锁，返回锁的唯一标识符，如果获取失败，则返回空字符串
// lockTimeout 是锁的持有时间
// acquireTimeout 是尝试获取锁的时间
std::string DistLock::acquireLock(redisContext* context, const std::string& lockName, int lockTimeout, int acquireTimeout) {
	std::string identifier = generateUUID();
	std::string lockKey = "lock:" + lockName;
	// 尝试的结束时间
	auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(acquireTimeout);
	
	// 如果现在的时间小于尝试的时间，因为可能别的人现在正持有锁，所以需要在一段时间间隔下继续尝试
	while (std::chrono::steady_clock::now() < endTime) {
		// 通过SET命令尝试加锁
		// 通过set命令设置，NX 表示只有当前面的key不存在才设置，EX 表示设置锁的自动释放时间，为lockTimeout
		redisReply* reply = (redisReply*)redisCommand(context, "SET %s %s NX EX %d", lockKey.c_str(), identifier.c_str(), lockTimeout);

		// 不为空，则可能设置成功
		if (reply != nullptr) {
			// 表示锁获取成功
			if (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK") {
				freeReplyObject(reply);
				return identifier;
			}

			freeReplyObject(reply);
		}

		// 等待1秒后再次执行
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	// 没有获得锁
	return "";
}

// 释放锁
bool DistLock::releaseLock(redisContext* context, const std::string& lockName, const std::string& identifier) {
	std::string lockKey = "lock:" + lockName;

	// 接下来需要执行锁的删除
	// 但是需要保证是原子操作，通过lua脚本来实现
	const char* luaScript = "if redis.call('get', KEYS[1]) == ARGV[1] then \
								return redis.call('del', KEYS[1]) \
						     else \
								return 0 \
							 end";

	// 调用eval命令执行
	redisReply* reply = (redisReply*)redisCommand(context, "EVAL %s 1 %s %s", luaScript, lockKey.c_str(), identifier.c_str());

	if (reply != nullptr) {
		// 检查是否执行成功
		if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) {
			freeReplyObject(reply);
			return true;
		}
		freeReplyObject(reply);
		return false;
	}

	return false;
}