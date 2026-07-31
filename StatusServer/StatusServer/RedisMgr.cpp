#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "DistLock.h"

RedisConfigPool::RedisConfigPool(size_t poolsize, const char* host, int port, const char* pwd) :
	_poolSize(poolsize), _host(host), _port(port), _b_stop(false) {
	for (size_t i = 0; i < poolsize; i++) {
		auto* context = (redisContext*)redisConnect(host, port);

		if (context == nullptr || context->err != 0) {
			if (context != nullptr) {
				redisFree(context);
			}
			continue;
		}

		auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
		if (reply->type == REDIS_REPLY_ERROR) {
			SPDLOG_ERROR("redis authentication failed during pool initialization");
			freeReplyObject(reply);
			redisFree(context);
			continue;
		}

		freeReplyObject(reply);
		SPDLOG_DEBUG("redis authentication succeeded during pool initialization");
		_connections.push(context);
	}
}

RedisConfigPool::~RedisConfigPool() {
	std::lock_guard<std::mutex> lock(_mutex);
	while (!_connections.empty()) {
		auto* context = _connections.front();
		redisFree(context);
		_connections.pop();
	}
}

void RedisConfigPool::close() {
	// 连接池要关闭了，将关闭状态置为true，同时唤醒所有还在等待连接的线程
	_b_stop = true;
	_conf.notify_all();
}

redisContext* RedisConfigPool::getConnection() {
	std::unique_lock<std::mutex> lock(_mutex);
	// 当连接池没有暂停，同时连接池内无空连接时，需要阻塞等待
	_conf.wait(lock, [this] {
		if (_b_stop) {
			return true;
		}
		return !_connections.empty();
		});

	if (_b_stop) {
		return nullptr;
	}

	auto* context = _connections.front();
	_connections.pop();
	return context;
}

void RedisConfigPool::returnConnection(redisContext* context) {
	std::lock_guard<std::mutex> lock(_mutex);
	_connections.push(context);
	_conf.notify_one();
}

RedisMgr::RedisMgr() {
	auto& grcpConfigMgr = ConfigMgr::GetInstance();
	auto host = grcpConfigMgr["Redis"]["Host"];
	auto port = grcpConfigMgr["Redis"]["Port"];
	auto pwd = grcpConfigMgr["Redis"]["Password"];
	_pool = std::make_unique<RedisConfigPool>(8, host.c_str(), atoi(port.c_str()), pwd.c_str());
}

RedisMgr::~RedisMgr() {
	Close();
}

bool RedisMgr::LPush(const std::string& key, const std::string& value) {
	auto connect = _pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis LPUSH failed, key={}, value_size={}, reason=null_reply", key, value.size());

		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
		SPDLOG_WARN("redis LPUSH failed, key={}, value_size={}, reason=unexpected_reply", key, value.size());
		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis LPUSH success, key={}, value_size={}", key, value.size());
	_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value) {
	auto connect = _pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "LPop %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis LPOP failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis LPOP miss, key={}", key);
		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);
	SPDLOG_DEBUG("redis LPOP success, key={}", key);
	_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
	auto connect = _pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis RPUSH failed, key={}, value_size={}, reason=null_reply", key, value.size());

		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (reply->type == REDIS_REPLY_INTEGER || reply->integer <= 0) {
		SPDLOG_WARN("redis RPUSH failed, key={}, value_size={}, reason=unexpected_reply", key, value.size());
		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis RPUSH success, key={}, value_size={}", key, value.size());
	_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value) {
	auto connect = _pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "RPop %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis RPOP failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis RPOP miss, key={}", key);
		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);
	SPDLOG_DEBUG("redis RPOP success, key={}", key);
	_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen) {
	const char* argv[4];
	size_t argvlen[4];
	argv[0] = "HSET";
	argvlen[0] = 4;
	argv[1] = key;
	argvlen[1] = strlen(key);
	argv[2] = hkey;
	argvlen[2] = strlen(hkey);
	argv[3] = hvalue;
	argvlen[3] = hvaluelen;
	auto connect = _pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
	if (reply == nullptr) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=null_reply", key, hkey, hvaluelen);

		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis HSET failed, key={}, field={}, value_size={}, reason=unexpected_reply", key, hkey, hvaluelen);
		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis HSET success, key={}, field={}, value_size={}", key, hkey, hvaluelen);
	_pool->returnConnection(connect);

	return true;
}

bool RedisMgr::ExistsKey(const std::string& key) {
	auto connect = _pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "exists %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis EXISTS failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
		SPDLOG_DEBUG("redis EXISTS miss, key={}", key);
		freeReplyObject(reply);
		_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis EXISTS success, key={}", key);
	_pool->returnConnection(connect);

	return true;
}

void RedisMgr::Close() {
	_pool->close();
}

/**
 * @brief
 * @param key
 * @param value 结果保存位置
 * @return
 * 1. 判断返回非空
 * 2. 判断返回非无效值
 * 3. 返回值类型必须为string
 */
bool RedisMgr::Get(const std::string& key, std::string& value) {
	auto connection = _pool->getConnection();
	if (connection == nullptr) {
		value = "";
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "GET %s", key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis GET failed, key={}, reason=null_reply", key); // 日志todo...
		value = "";
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis GET miss, key={}", key); // 日志todo...
		value = "";
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		SPDLOG_WARN("redis GET failed, key={}, reason=unexpected_type, type={}", key, reply->type); // 日志todo...
		return false;
	}

	value = reply->str;
	SPDLOG_DEBUG("redis GET success, key={}, value_size={}", key, value.size()); // 日志todo...

	return true;
}

/**
 * @brief
 * @param key
 * @param value
 * @return
 * 1. 返回非空
 * 2. 返回的类型为status类型
 * 3. status的内容状态为ok
 */
bool RedisMgr::Set(const std::string& key, const std::string& value) {
	auto connection = _pool->getConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "SET %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=null_reply", key, value.size()); // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (!(reply->type == REDIS_REPLY_STATUS &&
		(strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {
		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=unexpected_status, type={}", key, value.size(), reply->type); // 日志todo...
		return false;
	}

	SPDLOG_DEBUG("redis SET success, key={}, value_size={}", key, value.size()); // 日志todo...
	return true;
}

/**
 * @brief
 * @param first_key
 * @param second_key
 * @param value 结果保存位置
 * @return
 * 1. 返回非空
 * 2. 返回值非无效值
 * 3. 返回类型必须为string
 */
bool RedisMgr::HGet(const std::string& first_key, const std::string& second_key, std::string& value) {
	auto connection = _pool->getConnection();
	if (connection == nullptr) {
		value = "";
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "HGET %s %s", first_key.c_str(), second_key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis HGET failed, key={}, field={}, reason=null_reply", first_key, second_key); // 日志todo...
		value = "";
		_pool->returnConnection(connection);
		return false;
	}
	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis HGET miss, key={}, field={}", first_key, second_key); // 日志todo...
		value = "";
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		SPDLOG_WARN("redis HGET failed, key={}, field={}, reason=unexpected_type, type={}", first_key, second_key, reply->type); // 日志todo...
		value = "";
		return false;
	}

	value = reply->str;
	SPDLOG_DEBUG("redis HGET success, key={}, field={}, value_size={}", first_key, second_key, value.size()); //日志todo...
	return true;
}

/**
 * @brief
 * @param first_key
 * @param second_key
 * @param value
 * @return
 * 1. 返回非空
 * 2. 返回类型必须为integer （1为新增，0为更新已有）
 */
bool RedisMgr::HSet(const std::string& first_key, const std::string& second_key, const std::string& value) {
	auto connection = _pool->getConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "HSET %s %s %s", first_key.c_str(), second_key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=null_reply", first_key, second_key, value.size()); // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis HSET failed, key={}, field={}, value_size={}, reason=unexpected_type, type={}", first_key, second_key, value.size(), reply->type); // 日志todo...
		return false;
	}

	SPDLOG_DEBUG("redis HSET success, key={}, field={}, value_size={}", first_key, second_key, value.size()); //  日志todo...

	return true;
}

/**
 * @brief
 * @param first_key
 * @param second_key
 * @return
 * 1. 返回非空
 * 2. 返回类型必须为整数
 */
bool RedisMgr::HDel(const std::string& first_key, const std::string& second_key) {
	auto connection = _pool->getConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "HDEL %s %s", first_key.c_str(), second_key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis HDEL failed, key={}, field={}, reason=null_reply", first_key, second_key); // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis HDEL failed, key={}, field={}, reason=unexpected_type, type={}", first_key, second_key, reply->type); // 日志todo...
		return false;
	}

	SPDLOG_DEBUG("redis HDEL success, key={}, field={}", first_key, second_key); // 日志todo...

	return true;
}

/**
 * @brief
 * @param key
 * @return
 * 1. 返回非空
 * 2. 返回类型必须为整数
 */
bool RedisMgr::Del(const std::string& key) {
	auto connection = _pool->getConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "DEL %s", key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis DEL failed, key={}, reason=null_reply", key); // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, connection, reply]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis DEL failed, key={}, reason=unexpected_type, type={}", key, reply->type); // 日志todo...
		return false;
	}

	SPDLOG_DEBUG("redis DEL success, key={}", key); // 日志todo...

	return true;
}

// 如果加锁成功，则返回一个锁的唯一标识
std::string RedisMgr::acquireLock(const std::string& lockName, int lockTimeout, int acquireTimeout) {
	auto connection = _pool->getConnection();

	if (connection == nullptr) {
		return "";
	}

	Defer defer([this, connection]() {
		_pool->returnConnection(connection);
		});

	return DistLock::GetInstance()->acquireLock(connection, lockName, lockTimeout, acquireTimeout);
}

// 如果解锁成功，则返回true
bool RedisMgr::releaseLock(const std::string& lockName, const std::string& identifier) {
	if (identifier.empty()) {
		return true;
	}

	auto connection = _pool->getConnection();

	if (connection == nullptr) {
		return false;
	}

	Defer defer([this, connection]() {
		_pool->returnConnection(connection);
		});

	return DistLock::GetInstance()->releaseLock(connection, lockName, identifier);
}
