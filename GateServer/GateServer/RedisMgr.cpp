#include "RedisMgr.h"
#include "ConfigMgr.h"

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
			std::cout << "认证失败" << std::endl;
			freeReplyObject(reply);
			redisFree(context);
			continue;
		}

		freeReplyObject(reply);
		std::cout << "认证成功" << std::endl;
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

redisContext* RedisConfigPool::GetConnection() {
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
	auto pwd = grcpConfigMgr["Redis"]["Passwd"];
	_redis_pool = std::make_unique<RedisConfigPool>(5, host.c_str(), atoi(port.c_str()), pwd.c_str());
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
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl;
		value = "";
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);

	std::cout << "Execute command [ GET " << key << " ] success !" << std::endl;
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
		std::cout << "Execute command [ SET " << key << " " << value << " ]  failure !" << std::endl;
		std::cout << "reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (!(reply->type == REDIS_REPLY_STATUS &&
		(strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {
		std::cout << "reply->type : " << reply->type << std::endl;
		std::cout << "REDIS_REPLY_STRING : " << REDIS_REPLY_STRING << std::endl;
		std::cout << "Execute command [ SET " << key << " " << value << " ]  failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execute command [ SET " << key << " " << value << " ] success !" << std::endl;
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
		std::cout << "Execute command [ LPUSH " << key << " " << value << " ] failure !" << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
		std::cout << "Execute command [ LPUSH " << key << " " << value << " ] failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execute command [ LPUSH " << key << " " << value << " ] success !" << std::endl;
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
		std::cout << "Execute command [ LPOP " << key << " ] failure !" << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execute command [ LPOP " << key << " ] failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);
	std::cout << "Execute command [ LPOP " << key << " ] success !" << std::endl;
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
		std::cout << "Execute command [ RPUSH " << key << " " << value << " ] failure !" << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	// 执行失败
	if (reply->type == REDIS_REPLY_INTEGER || reply->integer <= 0) {
		std::cout << "Execute command [ RPUSH " << key << " " << value << " ] failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execute command [ LPUSH " << key << " " << value << " ] success !" << std::endl;
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
		std::cout << "Execute command [ RPOP " << key << " ] failure !" << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execute command [ RPOP " << key << " ] failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);
	std::cout << "Execute command [ RPOP " << key << " ] success !" << std::endl;
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
		std::cout << "Execute command [ HSET " << key << " " << hkey << " " << value << " ] failure !" << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execute command [ HSET " << key << " " << hkey << " " << value << " ] failure !" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execute command [ HSET " << key << " " << hkey << " " << value << " ] success !" << std::endl;
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
		std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] failure ! " << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] failure ! " << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] success ! " << std::endl;
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
		std::cout << "Execut command [ HGet " << key << " " << hkey << "  ] failure ! " << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		_redis_pool->returnConnection(connect);
		return "";
	}

	if (reply->type == REDIS_REPLY_NIL) {
		freeReplyObject(reply);
		std::cout << "Execut command [ HGet " << key << " " << hkey << "  ] failure ! " << std::endl;
		_redis_pool->returnConnection(connect);
		return "";
	}
	value = reply->str;

	freeReplyObject(reply);
	std::cout << "Execut command [ HGet " << key << " " << hkey << " ] success ! " << std::endl;
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
		std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
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
		std::cout << "Not Found [ Key " << key << " ]  ! " << std::endl;
		std::cout << "Reply is nullptr" << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
		std::cout << "Not Found [ Key " << key << " ]  ! " << std::endl;
		freeReplyObject(reply);
		_redis_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
	_redis_pool->returnConnection(connect);
	
	return true;
}

void RedisMgr::Close() {
	_redis_pool->close();
}