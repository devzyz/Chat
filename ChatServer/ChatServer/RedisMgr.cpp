#include "RedisMgr.h"

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
				std::cout << "认证失败" << std::endl;
				freeReplyObject(reply);
				continue;
			}

			// 认证成功
			std::cout << "认证成功" << std::endl;
			freeReplyObject(reply);
			_que.push(context);
		}
	}
	catch (std::exception& e) {
		std::cout << "create RedisConnectionPool is failure, error is " << e.what() << std::endl;
	}
}

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
	if (_b_stop) {
		return;
	}
	_que.push(connection);
	_cond.notify_one();
}

void RedisConnectionPool::close() {
	_b_stop = true;
	_cond.notify_all();
}