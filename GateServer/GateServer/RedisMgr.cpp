#include "RedisMgr.h"

RedisMgr::RedisMgr() {

}

RedisMgr::~RedisMgr() {

}

bool RedisMgr::Connect(const std::string& host, int port) {
	this->_connect = redisConnect(host.c_str(), port);
	if (this->_connect == nullptr) return false;
	if (this->_connect->err) {
		std::cout << "connect error " << this->_connect->errstr << std::endl;
		redisFree(this->_connect);
		return false;
	}

	return true;
}

bool RedisMgr::Get(const std::string& key, std::string& value) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "GET %s", key.c_str());

	if (this->_reply == nullptr) {
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	if (this->_reply->type != REDIS_REPLY_STRING) {
		std::cout << "Execute command [ Get " << key << " ]  failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	value = this->_reply->str;
	freeReplyObject(this->_reply);

	std::cout << "Execute command [ GET " << key << " ] success !" << std::endl;
	return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value) {
	// 执行set命令
	this->_reply = (redisReply*)redisCommand(this->_connect, "SET %s %s", key.c_str(), value.c_str());

	if (this->_reply == nullptr) {
		std::cout << "Execute command [ SET " << key << " " << value << " ]  failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	// 执行失败
	if (!(this->_reply->type == REDIS_REPLY_STRING &&
		(strcmp(this->_reply->str, "OK") == 0 || strcmp(this->_reply->str, "ok") == 0))) {
		std::cout << "Execute command [ SET " << key << " " << value << " ]  failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execute command [ SET " << key << " " << value << " ] success !" << std::endl;
	return true;
}

bool RedisMgr::Auth(const std::string& password) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "AUTH %s", password.c_str());

	if (this->_reply == nullptr) {
		std::cout << "Execute command [ Auth " << password << " ] failure, 认证失败！" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	// 执行失败
	if (this->_reply->type == REDIS_REPLY_ERROR) {
		std::cout << "Execute command [ Auth " << password << " ]  failure，认证失败！" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execute command [ Auth " << password << " ] success, 认证成功！" << std::endl;
	return true;
}

bool RedisMgr::LPush(const std::string& key, const std::string& value) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "LPUSH %s %s", key.c_str(), value.c_str());

	if (this->_reply == nullptr) {
		std::cout << "Execute command [ LPUSH " << key << " " << value << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	// 执行失败
	if (this->_reply->type == REDIS_REPLY_INTEGER || this->_reply->integer <= 0) {
		std::cout << "Execute command [ LPUSH " << key << " " << value << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execute command [ LPUSH " << key << " " << value << " ] success !" << std::endl;
	return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "LPop %s", key.c_str());
	if (this->_reply == nullptr || this->_reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execute command [ LPOP " << key << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	value = _reply->str;
	freeReplyObject(this->_reply);
	std::cout << "Execute command [ LPOP " << key << " ] success !" << std::endl;
	return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "RPUSH %s %s", key.c_str(), value.c_str());

	if (this->_reply == nullptr) {
		std::cout << "Execute command [ RPUSH " << key << " " << value << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	// 执行失败
	if (this->_reply->type == REDIS_REPLY_INTEGER || this->_reply->integer <= 0) {
		std::cout << "Execute command [ RPUSH " << key << " " << value << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execute command [ LPUSH " << key << " " << value << " ] success !" << std::endl;
	return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "RPop %s", key.c_str());
	if (this->_reply == nullptr || this->_reply->type == REDIS_REPLY_NIL) {
		std::cout << "Execute command [ RPOP " << key << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	value = _reply->str;
	freeReplyObject(this->_reply);
	std::cout << "Execute command [ RPOP " << key << " ] success !" << std::endl;
	return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
	if (this->_reply == nullptr || this->_reply->type == REDIS_REPLY_INTEGER) {
		std::cout << "Execute command [ HSET " << key << " " << hkey << " " << value << " ] failure !" << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execute command [ HSET " << key << " " << hkey << " " << value << " ] success !" << std::endl;
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
	this->_reply = (redisReply*)redisCommandArgv(this->_connect, 4, argv, argvlen);
	if (_reply == nullptr || _reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] failure ! " << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] success ! " << std::endl;
	
	return true;
}

std::string RedisMgr::HGet(const std::string& key, const std::string& hkey) {
	const char* argv[3];
	size_t argvlen[3];
	argv[0] = "HGET";
	argvlen[0] = 4;
	argv[1] = key.c_str();
	argvlen[1] = key.length();
	argv[2] = hkey.c_str();
	argvlen[2] = hkey.length();
	this->_reply = (redisReply*)redisCommandArgv(this->_connect, 3, argv, argvlen);
	if (this->_reply == nullptr || this->_reply->type == REDIS_REPLY_NIL) {
		freeReplyObject(this->_reply);
		std::cout << "Execut command [ HGet " << key << " " << hkey << "  ] failure ! " << std::endl;
		return "";
	}
	std::string value = this->_reply->str;

	freeReplyObject(this->_reply);
	std::cout << "Execut command [ HGet " << key << " " << hkey << " ] success ! " << std::endl;

	return value;
}

bool RedisMgr::Del(const std::string& key) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "DEL %s", key.c_str());
	if (this->_reply == nullptr || this->_reply->type != REDIS_REPLY_INTEGER) {
		std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
	
	return true;
}

bool RedisMgr::ExistsKey(const std::string& key) {
	this->_reply = (redisReply*)redisCommand(this->_connect, "exists %s", key.c_str());
	if (this->_reply == nullptr || this->_reply->type != REDIS_REPLY_INTEGER || this->_reply->integer == 0) {
		std::cout << "Not Found [ Key " << key << " ]  ! " << std::endl;
		freeReplyObject(this->_reply);
		return false;
	}

	freeReplyObject(this->_reply);
	std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
	
	return true;
}

void RedisMgr::Close() {
	redisFree(this->_connect);
	this->_connect = nullptr;
}