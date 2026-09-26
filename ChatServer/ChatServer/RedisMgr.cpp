#include "RedisMgr.h"
#include <boost/asio.hpp>
#include "ConfigMgr.h"
#include "Const.h"
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
 *
 *
 * @param value 结果保存位置
 *
 * 1. 判断返回非空
 * 2. 判断返回非无效值
 * 3. 返回值类型必须为string
 */
bool RedisMgr::Get(const std::string& key, std::string& value) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		value = "";
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "GET %s", key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis GET failed, key={}, reason=null_reply", key);
		value = "";
		_pool->ReturnConnection(connection);
		return false;
	}

	Defer defer(/** @brief 释放 Redis 响应并归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, reply, connection]() {
		freeReplyObject(reply);
		_pool->ReturnConnection(connection);
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
 *
 *
 *
 *
 * 1. 返回非空
 * 2. 返回的类型为status类型
 * 3. status的内容状态为ok
 */
bool RedisMgr::Set(const std::string& key, const std::string& value) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "SET %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=null_reply", key, value.size());
		_pool->ReturnConnection(connection);
		return false;
	}

	Defer defer(/** @brief 释放 Redis 响应并归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, reply, connection]() {
		freeReplyObject(reply);
		_pool->ReturnConnection(connection);
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
 *
 *
 *
 * @param value 结果保存位置
 *
 * 1. 返回非空
 * 2. 返回值非无效值
 * 3. 返回类型必须为string
 */
bool RedisMgr::HGet(const std::string& first_key, const std::string& second_key, std::string& value) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		value = "";
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "HGET %s %s", first_key.c_str(), second_key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis HGET failed, key={}, field={}, reason=null_reply", first_key, second_key);
		value = "";
		_pool->ReturnConnection(connection);
		return false;
	}

	Defer defer(/** @brief 释放 Redis 响应并归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, reply, connection]() {
		freeReplyObject(reply);
		_pool->ReturnConnection(connection);
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
 *
 *
 *
 *
 *
 * 1. 返回非空
 * 2. 返回类型必须为integer （1为新增，0为更新已有）
 */
bool RedisMgr::HSet(const std::string& first_key, const std::string& second_key, const std::string& value) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "HSET %s %s %s", first_key.c_str(), second_key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=null_reply", first_key, second_key, value.size());
		_pool->ReturnConnection(connection);
		return false;
	}

	Defer defer(/** @brief 释放 Redis 响应并归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, reply, connection]() {
		freeReplyObject(reply);
		_pool->ReturnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=unexpected_type, type={}", first_key, second_key, value.size(), reply->type);
		return false;
	}

	SPDLOG_DEBUG("redis HSET success, key={}, field={}, value_size={}", first_key, second_key, value.size());

	return true;
}

/**
 *
 *
 *
 *
 * 1. 返回非空
 * 2. 返回类型必须为整数
 */
bool RedisMgr::HDel(const std::string& first_key, const std::string& second_key) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "HDEL %s %s", first_key.c_str(), second_key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis HDEL failed, key={}, field={}, reason=null_reply", first_key, second_key);
		_pool->ReturnConnection(connection);
		return false;
	}

	Defer defer(/** @brief 释放 Redis 响应并归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, reply, connection]() {
		freeReplyObject(reply);
		_pool->ReturnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_ERROR("redis HDEL failed, key={}, field={}, reason=unexpected_type, type={}", first_key, second_key, reply->type);
		return false;
	}

	SPDLOG_DEBUG("redis HDEL success, key={}, field={}", first_key, second_key);

	return true;
}

/**
 *
 *
 *
 * 1. 返回非空
 * 2. 返回类型必须为整数
 */
bool RedisMgr::Del(const std::string& key) {
	auto connection = _pool->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connection, "DEL %s", key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis DEL failed, key={}, reason=null_reply", key);
		_pool->ReturnConnection(connection);
		return false;
	}

	Defer defer(/** @brief 释放 Redis 响应并归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, connection, reply]() {
		freeReplyObject(reply);
		_pool->ReturnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_ERROR("redis DEL failed, key={}, reason=unexpected_type, type={}", key, reply->type);
		return false;
	}

	SPDLOG_DEBUG("redis DEL success, key={}", key);

	return true;
}


/**
 *
 *
 */
void RedisMgr::Close() {
	_pool->Close();
}

std::optional<std::vector<std::string>> RedisMgr::Eval(const std::string& script,
    const std::vector<std::string>& keys, const std::vector<std::string>& arguments) {
    auto* connection = _pool->GetConnection();
    if (!connection) return std::nullopt;
    Defer release(/** @brief 归还本次借用的数据库连接，退出作用域后不得再使用。 */ [this, connection] { _pool->ReturnConnection(connection); });
    const timeval timeout{2, 0};
    if (redisSetTimeout(connection, timeout) != REDIS_OK) return std::nullopt;
    std::vector<std::string> command{"EVAL", script, std::to_string(keys.size())};
    command.insert(command.end(), keys.begin(), keys.end());
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::vector<const char*> argv;
    std::vector<std::size_t> lengths;
    for (const auto& value : command) { argv.push_back(value.data()); lengths.push_back(value.size()); }
    auto* reply = static_cast<redisReply*>(redisCommandArgv(connection,
        static_cast<int>(argv.size()), argv.data(), lengths.data()));
    if (!reply) return std::nullopt;
    Defer free_reply(/** @brief 释放本次 Redis 命令响应，保证异常路径也回收内存。 */ [reply] { freeReplyObject(reply); });
    if (reply->type != REDIS_REPLY_ARRAY) return std::nullopt;
    std::vector<std::string> result;
    for (std::size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type != REDIS_REPLY_STRING) return std::nullopt;
        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
    }
    return result;
}
