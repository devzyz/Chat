#include "RedisMgr.h"
#include "ConfigMgr.h"


RedisMgr::RedisMgr() {
	auto& grcpConfigMgr = ConfigMgr::GetInstance();
	auto host = grcpConfigMgr["Redis"]["Host"];
	auto port = grcpConfigMgr["Redis"]["Port"];
	auto pwd = grcpConfigMgr["Redis"]["Password"];
	_redis_pool = std::make_unique<RedisConfigPool>(8, host.c_str(), atoi(port.c_str()), pwd.c_str());
}

RedisMgr::~RedisMgr() {
	Close();
}

bool RedisMgr::Get(const std::string& key, std::string& value) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply= (redisReply*)redisCommand(connect, "GET %s", key.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis GET failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis GET miss, key={}", key);
		value = "";
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		SPDLOG_WARN("redis GET failed, key={}, reason=unexpected_type, type={}", key, reply->type);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);

	SPDLOG_DEBUG("redis GET success, key={}, value_size={}", key, value.size());
	_redis_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value) {
	// 执行set命令
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=null_reply", key, value.size());

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (!(reply->type == REDIS_REPLY_STATUS &&
		(strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {


		SPDLOG_ERROR("redis SET failed, key={}, value_size={}, reason=unexpected_status, type={}", key, value.size(), reply->type);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis SET success, key={}, value_size={}", key, value.size());
	_redis_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::LPush(const std::string& key, const std::string& value) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis LPUSH failed, key={}, value_size={}, reason=null_reply", key, value.size());

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
		SPDLOG_WARN("redis LPUSH failed, key={}, value_size={}, reason=unexpected_reply", key, value.size());
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis LPUSH success, key={}, value_size={}", key, value.size());
	_redis_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "LPop %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis LPOP failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis LPOP miss, key={}", key);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);
	SPDLOG_DEBUG("redis LPOP success, key={}", key);
	_redis_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());

	if (reply == nullptr) {
		SPDLOG_ERROR("redis RPUSH failed, key={}, value_size={}, reason=null_reply", key, value.size());

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (reply->type == REDIS_REPLY_INTEGER || reply->integer <= 0) {
		SPDLOG_WARN("redis RPUSH failed, key={}, value_size={}, reason=unexpected_reply", key, value.size());
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis RPUSH success, key={}, value_size={}", key, value.size());
	_redis_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "RPop %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis RPOP failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		SPDLOG_DEBUG("redis RPOP miss, key={}", key);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);
	SPDLOG_DEBUG("redis RPOP success, key={}", key);
	_redis_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=null_reply", key, hkey, value.size());

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis HSET failed, key={}, field={}, value_size={}, reason=unexpected_reply", key, hkey, value.size());
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis HSET success, key={}, field={}, value_size={}", key, hkey, value.size());
	_redis_pool->returnConnection(connect);
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
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
	if (reply == nullptr) {
		SPDLOG_ERROR("redis HSET failed, key={}, field={}, value_size={}, reason=null_reply", key, hkey, hvaluelen);

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis HSET failed, key={}, field={}, value_size={}, reason=unexpected_reply", key, hkey, hvaluelen);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis HSET success, key={}, field={}, value_size={}", key, hkey, hvaluelen);
	_redis_pool->returnConnection(connect);

	return true;
}

bool RedisMgr::HGet(const std::string& key, const std::string& hkey, std::string& value) {
	const char* argv[3];
	size_t argvlen[3];
	argv[0] = "HGET";
	argvlen[0] = 4;
	argv[1] = key.c_str();
	argvlen[1] = key.length();
	argv[2] = hkey.c_str();
	argvlen[2] = hkey.length();
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommandArgv(connect, 3, argv, argvlen);
	if (reply == nullptr) {
		freeReplyObject(reply);
		SPDLOG_ERROR("redis HGET failed, key={}, field={}, reason=null_reply", key, hkey);

		_redis_pool->returnConnection(connect);
		return "";
	}

	if (reply->type == REDIS_REPLY_NIL) {
		freeReplyObject(reply);
		SPDLOG_WARN("redis HGET failed, key={}, field={}, reason=unexpected_reply", key, hkey);
		_redis_pool->returnConnection(connect);
		return "";
	}
	value = reply->str;

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis HGET success, key={}, field={}, value_size={}", key, hkey, value.size());
	_redis_pool->returnConnection(connect);

	return true;
}

bool RedisMgr::Del(const std::string& key) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis DEL failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		SPDLOG_WARN("redis DEL failed, key={}, reason=unexpected_reply", key);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis DEL success, key={}", key);
	_redis_pool->returnConnection(connect);

	return true;
}

bool RedisMgr::ExistsKey(const std::string& key) {
	auto connect = _redis_pool->GetConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "exists %s", key.c_str());
	if (reply == nullptr) {
		SPDLOG_ERROR("redis EXISTS failed, key={}, reason=null_reply", key);

		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
		SPDLOG_DEBUG("redis EXISTS miss, key={}", key);
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	SPDLOG_DEBUG("redis EXISTS success, key={}", key);
	_redis_pool->returnConnection(connect);

	return true;
}

void RedisMgr::Close() {
	_redis_pool->close();
}
