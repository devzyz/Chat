#pragma once
#include "Singleton.h"
#include "hiredis/hiredis.h"
#include "const.h"

class RedisConfigPool {
public:
	RedisConfigPool(size_t poolsize, const char* host, int port, const char* pwd);

	~RedisConfigPool();

	void close();
	redisContext* GetConnection();
	void returnConnection(redisContext * context);

private:
	const char* _host;
	int _port;
	size_t _poolSize;
	std::atomic<bool> _b_stop;
	std::mutex _mutex;
	std::condition_variable _conf;
	std::queue<redisContext*> _connections;
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

