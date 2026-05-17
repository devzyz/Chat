#pragma once
#include "Singleton.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <hiredis/hiredis.h>

class RedisConnectionPool {
public:
	RedisConnectionPool(const std::string& host, const std::string& port, const std::string& password, int poolSize);
	~RedisConnectionPool();
	redisContext* getConnection();
	void returnConnection(redisContext* connection);
	void close();
private:
	std::atomic<bool> _b_stop;

	const std::string _host;
	const std::string _port;
	const std::string _password;
	
	std::mutex _que_mutex;
	std::condition_variable _cond;
	std::queue<redisContext*> _que;
	int _pool_size;
};

class RedisMgr : Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr();
private:
	RedisMgr();
	std::unique_ptr<RedisConnectionPool> _pool;
};

