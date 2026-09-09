#pragma once
#include "../../common/redis/RedisPool.h"
#include "Singleton.h"
#include "hiredis/hiredis.h"
#include "const.h"

class RedisConfigPool {
public:
    RedisConfigPool(std::size_t pool_size, const char* host, int port, const char* password)
        : _transport(host, port, password, pool_size) {}
    redisContext* GetConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        return _transport.Borrow(timeout);
    }
    void returnConnection(redisContext* connection) { _transport.Return(connection); }
    void close() { _transport.Close(); }
private:
    chat_redis::RedisPool _transport;
};

class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr();
	bool Get(const std::string& key, std::string& value);
	bool Set(const std::string& key, const std::string& value);
	bool LPush(const std::string& key, const std::string& value);
	bool LPop(const std::string& key, std::string& value);
	bool RPush(const std::string& key, const std::string& value);
	bool RPop(const std::string& key, std::string& value);
	bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
	bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
	bool HGet(const std::string& key, const std::string& hkey, std::string& value);
	bool Del(const std::string& key);
	bool ExistsKey(const std::string& key);
	void Close();
protected:
	RedisMgr();

	std::unique_ptr<RedisConfigPool> _redis_pool;
};
