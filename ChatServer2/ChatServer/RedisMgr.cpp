#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "Const.h"

RedisConnectionPool::RedisConnectionPool(const std::string& host, const std::string& port, const std::string& password, int poolSize)
	: _host(host), _port(port), _password(password), _pool_size(poolSize), _b_stop(false) {
	try {
		for (int i = 0; i < _pool_size; i++) {
			auto* context = redisConnect(host.c_str(), atoi(port.c_str()));

			// 连接失败
			if (context == nullptr || context->err != 0) {
				if (context != nullptr) {
					redisFree(context);
				}
				return;
			}

			auto reply = (redisReply*)redisCommand(context, "AUTH %s", password.c_str());
			if (reply->type == REDIS_REPLY_ERROR) {
				std::cout << "认证失败" << std::endl; // 日志todo...
				freeReplyObject(reply);
				continue;
			}

			// 认证成功
			std::cout << "认证成功" << std::endl; // 日志todo...
			freeReplyObject(reply);
			_que.push(context);
		}
	}
	catch (std::exception& e) {
		std::cout << "create RedisConnectionPool is failure, error is " << e.what() << std::endl; // 日志todo...
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
	_b_stop = true;
	_cond.notify_all();
}

RedisMgr::RedisMgr() {
	auto& configMgr = ConfigMgr::GetInstance();
	auto host = configMgr["Redis"]["Host"];
	auto port = configMgr["Redis"]["Port"];
	auto password = configMgr["Redis"]["Password"];

	_pool = std::make_unique<RedisConnectionPool>(host, port, password, 2);
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
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl; // 日志todo...
		value = "";
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl; // 日志todo...
		value = "";
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl; // 日志todo...
		return false;
	}

	value = reply->str;
	std::cout << "Execute command [ GET " << key << " ] success !" << std::endl; // 日志todo...

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
		std::cout << "Execute command [ SET " << key << " " << value << " ]  failure !" << std::endl; // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (!(reply->type == REDIS_REPLY_STATUS &&
		(strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {
		std::cout << "Execute command [ SET " << key << " " << value << " ]  failure !" << std::endl; // 日志todo...
		return false;
	}

	std::cout << "Execute command [ SET " << key << " " << value << " ] success !" << std::endl; // 日志todo...
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
		std::cout << "Execut command [ HGet " << first_key << " " << second_key << "  ] failure ! " << std::endl; // 日志todo...
		value = "";
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execut command [ HGet " << first_key << " " << second_key << "  ] failure ! " << std::endl; // 日志todo...
		value = "";
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		std::cout << "Execut command [ HGet " << first_key << " " << second_key << "  ] failure ! " << std::endl; // 日志todo...
		value = "";
		return false;
	}

	value = reply->str;
	std::cout << "Execute command [ HGET " << first_key << " " << second_key << " ] success !" << std::endl; //日志todo...
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
		std::cout << "Execute command [ HSET " << first_key << " " << second_key << " " << value << " ] failure !"
			<< std::endl; // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execute command [ HSET " << first_key << " " << second_key << " " << value << " ] failure !"
			<< std::endl; // 日志todo...
		return false;
	}

	std::cout << "Execute command [ HSET " << first_key << " " << second_key << " " << value << " ] success !"
		<< std::endl; //  日志todo...

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
		std::cout << "Execute command [ HDEL " << first_key << " " << second_key << " ] failure !"
			<< std::endl; // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, reply, connection]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execute command [ HDEL " << first_key << " " << second_key << " ] failure !"
			<< std::endl; // 日志todo...
		return false;
	}

	std::cout << "Execute command [ HDEL " << first_key << " " << second_key << " ] success !"
		<< std::endl; // 日志todo...

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
		std::cout << "Execute command [ DEL " << key << " ] failure !" << std::endl; // 日志todo...
		_pool->returnConnection(connection);
		return false;
	}

	Defer defer([this, connection, reply]() {
		freeReplyObject(reply);
		_pool->returnConnection(connection);
		});

	if (reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execute command [ DEL " << key << " ] failure !" << std::endl; // 日志todo...
		return false;
	}

	std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl; // 日志todo...

	return true;
}

/**
 * @brief
 * @return
 */
void RedisMgr::Close() {
	_pool->close();
}
