#pragma once
#include "Singleton.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <hiredis/hiredis.h>

// redis连接池
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
private:
	RedisMgr();
	std::unique_ptr<RedisConnectionPool> _pool;
};

