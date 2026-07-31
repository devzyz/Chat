#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "Const.h"
#include "DistLock.h"
#include "LogMgr.h"

RedisConnectionPool::RedisConnectionPool(const std::string& host, const std::string& port, const std::string& password, int poolSize)
	: _host(host), _port(port), _password(password), _pool_size(poolSize), _b_stop(false), _fail_count(0) {
	try {
		for (int i = 0; i < _pool_size; i++) {
			auto* context = redisConnect(_host.c_str(), atoi(_port.c_str()));

			// 连接失败
			if (context == nullptr || context->err != 0) {
				if (context != nullptr) {
					redisFree(context);
				}
				return;
			}

			auto reply = (redisReply*)redisCommand(context, "AUTH %s", _password.c_str());
			if (reply->type == REDIS_REPLY_ERROR) {
				SPDLOG_ERROR("redis auth failed while initializing pool, host={}, port={}", _host, _port);
				freeReplyObject(reply);
				redisFree(context);
				continue;
			}

			SPDLOG_DEBUG("redis auth succeeded while initializing pool, host={}, port={}", _host, _port);
			freeReplyObject(reply);
			_que.push(context);
		}

		// 进行心跳
		_check_thread = std::thread([this]() {
			int count = 0;
			while (!_b_stop) {
				if (count >= 60) {
					CheckConnection();
					count = 0;
					continue;
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
				count++;
			}
			});
	}
	catch (std::exception& e) {
		SPDLOG_ERROR("create Redis connection pool failed, error={}", e.what());
	}
}

// 析构，释放所有的与redis的连接
RedisConnectionPool::~RedisConnectionPool() {
	std::lock_guard<std::mutex> lock(_que_mutex);
	close();
	while (_que.size()) {
		auto* context = _que.front();
		redisFree(context);
		_que.pop();
	}
}

redisContext* RedisConnectionPool::getConnection() {
	std::unique_lock<std::mutex> lock(_que_mutex);
	_cond.wait(lock, [this]() {
		if (_b_stop) {
			return true;
		}
		return !_que.empty();
		});

	if (_b_stop) {
		return nullptr;
	}
	auto connection = _que.front();
	_que.pop();
	return connection;
}

void RedisConnectionPool::returnConnection(redisContext* connection) {
	std::lock_guard<std::mutex> lock(_que_mutex);
	_que.push(connection);
	_cond.notify_one();
}

void RedisConnectionPool::close() {
	if (_b_stop) {
		return;
	}
	_b_stop = true;
	_cond.notify_all();
	_check_thread.join();
}

// 心跳保活
void RedisConnectionPool::CheckConnection() {
	std::size_t target_count;
	// 加锁获取到需要心跳的数量
	{
		std::lock_guard<std::mutex> lock(_que_mutex);
		target_count = _que.size();
	}

	// 如果还需要保活的数量不为零，并且需要保活
	while (target_count > 0 && !_b_stop) {
		redisContext* context = nullptr;
		// 取出一个连接
		{
			std::lock_guard<std::mutex> lock(_que_mutex);
			// 如果为空，则说明当前时间间隔内，心跳已经完成
			if (_que.empty()) {
				break;
			}

			context = _que.front();
			_que.pop();
		}

		// 开始进行心跳
		redisReply* reply = nullptr;
		try {
			// 执行心跳
			reply = (redisReply*)redisCommand(context, "PING");
			
			// 底层i/o协议有没有问题
			if (context->err) {
				SPDLOG_WARN("redis heartbeat connection error, err={}, fail_count={}", context->err, _fail_count.load());
				if (reply) {
					freeReplyObject(reply);
				}
				redisFree(context);
				_fail_count++;
				continue;
			}

			// redis自身返回是不是error
			if (!reply || reply->type == REDIS_REPLY_ERROR) {
				SPDLOG_WARN("redis heartbeat reply invalid, err={}, fail_count={}", context->err, _fail_count.load());
				if (reply) {
					freeReplyObject(reply);
				}
				redisFree(context);
				_fail_count++;
				continue;
			}

			// 没问题，放回连接池
			freeReplyObject(reply);
			returnConnection(context);
		}
		catch (std::exception& e) {
			// 如果失败，则将失败数量加一，等待后面重连
			SPDLOG_WARN("redis heartbeat exception, error={}, fail_count={}", e.what(), _fail_count.load());
			if (reply) {
				freeReplyObject(reply);
			}
			redisFree(context);
			_fail_count++;
		}
	}
	
	int retry = 0;
	while (_fail_count > 0 && retry < REDIS_MAX_RETRIES) {
		bool success = reconnection();
		if (success) {
			_fail_count--;
		}
		else {
			retry++;
		}
	}
}

bool RedisConnectionPool::reconnection() {
	auto* context = redisConnect(_host.c_str(), atoi(_port.c_str()));

	// 连接失败
	if (context == nullptr || context->err != 0) {
		if (context != nullptr) {
			redisFree(context);
		}
		return false;
	}

	auto reply = (redisReply*)redisCommand(context, "AUTH %s", _password.c_str());
	if (reply->type == REDIS_REPLY_ERROR) {
		SPDLOG_WARN("redis auth failed while reconnecting, host={}, port={}", _host, _port);
		freeReplyObject(reply);
		redisFree(context);
		return false;
	}

	SPDLOG_INFO("redis auth succeeded while reconnecting, host={}, port={}", _host, _port);
	freeReplyObject(reply);
	{
		std::lock_guard<std::mutex> lock(_que_mutex);
		_que.push(context);
	}
	return true;
}

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
