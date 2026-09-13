#pragma once
#include "../../common/redis/RedisPool.h"
#include "Singleton.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <chrono>
#include <hiredis/hiredis.h>

class RedisConnectionPool {
public:
    RedisConnectionPool(const std::string& host, const std::string& port,
        const std::string& password, int pool_size)
        : _transport(host, ParsePort(port), password, pool_size > 0 ? static_cast<std::size_t>(pool_size) : 0) {}
    redisContext* getConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        return _transport.Borrow(timeout);
    }
    void returnConnection(redisContext* connection) { _transport.Return(connection); }
    void close() { _transport.Close(); }
private:
    static int ParsePort(const std::string& port) {
        std::size_t parsed = 0;
        const int value = std::stoi(port, &parsed);
        if (parsed != port.size() || value < 1 || value > 65535) {
            throw std::invalid_argument("invalid Redis port");
        }
        return value;
    }

    chat_redis::RedisPool _transport;
};

/**
 * @brief 
 * 连接redis的单例类
 */
class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr();
	// 结果保存到value内
	bool Get(const std::string& key, std::string& value);
	bool Set(const std::string& key, const std::string& value);
	// 结果保存到value内
	bool HGet(const std::string& first_key, const std::string& second_key, std::string& value);
	bool HSet(const std::string& first_key, const std::string& second_key, const std::string& value);
	bool HDel(const std::string& first_key, const std::string& second_key);
	bool Del(const std::string& key);
	void Close();

	std::string acquireLock(const std::string& lockName, int lockTimeout, int acquireTimeout);
	bool releaseLock(const std::string& lockName, const std::string& identifier);
private:
	RedisMgr();
	std::unique_ptr<RedisConnectionPool> _pool;
};
