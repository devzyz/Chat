#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "Const.h"
#include "DistLock.h"
#include "LogMgr.h"


RedisMgr::RedisMgr() {
	auto& configMgr = ConfigMgr::GetInstance();
	auto host = configMgr["Redis"]["Host"];
	auto port = configMgr["Redis"]["Port"];
	auto password = configMgr["Redis"]["Password"];

	_pool = std::make_unique<RedisConnectionPool>(host, port, password, 8);
}

RedisMgr::~RedisMgr() {
	Close();
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
		SPDLOG_ERROR("redis GET failed, key={}, reason=null_reply", key);
		value = "";
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis GET miss, key={}", key);
		value = "";
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		SPDLOG_WARN("redis GET failed, key={}, reason=unexpected_type, type={}", key, reply->type);
		return false;
	}

	value = reply->str;
	SPDLOG_DEBUG("redis GET success, key={}, value_size={}", key, value.size());

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
		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=null_reply", key, value.size());
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (!(reply->type == REDIS_REPLY_STATUS &&
		(strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {
		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=unexpected_status", key, value.size());
		return false;
	}

	SPDLOG_DEBUG("redis SET success, key={}, value_size={}", key, value.size());
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
		SPDLOG_ERROR("redis HGET failed, key={}, field={}, reason=null_reply", first_key, second_key);
		value = "";
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis HGET miss, key={}, field={}", first_key, second_key);
		value = "";
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		SPDLOG_WARN("redis HGET failed, key={}, field={}, reason=unexpected_type, type={}", first_key, second_key, reply->type);
		value = "";
		return false;
	}

	value = reply->str;
	SPDLOG_DEBUG("redis HGET success, key={}, field={}, value_size={}", first_key, second_key, value.size());
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
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=null_reply", first_key, second_key, value.size());
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=unexpected_type, type={}", first_key, second_key, value.size(), reply->type);
		return false;
	}

	SPDLOG_DEBUG("redis HSET success, key={}, field={}, value_size={}", first_key, second_key, value.size());

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
		SPDLOG_ERROR("redis HDEL failed, key={}, field={}, reason=null_reply", first_key, second_key);
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_ERROR("redis HDEL failed, key={}, field={}, reason=unexpected_type, type={}", first_key, second_key, reply->type);
		return false;
	}

	SPDLOG_DEBUG("redis HDEL success, key={}, field={}", first_key, second_key);

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
		SPDLOG_ERROR("redis DEL failed, key={}, reason=null_reply", key);
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, connection, reply]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_ERROR("redis DEL failed, key={}, reason=unexpected_type, type={}", key, reply->type);
		return false;
	}

	SPDLOG_DEBUG("redis DEL success, key={}", key);

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

/**
 * @brief
 * @return
 */
void RedisMgr::Close() {
	_pool->close();
}
