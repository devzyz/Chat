#include "RedisMgr.h"
#include "../../common/redis/Reply.h"
#include "ConfigMgr.h"
#include <stdexcept>

bool RedisMgr::PutExpiringToken(int uid, const std::string& value, long long auth_version) {
    const auto key = std::to_string(uid);
    const auto field = "utoken_" + key;
    auto* connection = _redis_pool->GetConnection();
    if (!connection)
        return false;
    // 归还独占 Redis 连接。
    Defer release([this, connection] {
        _redis_pool->ReturnConnection(connection);
    });
    const std::string script = "if (redis.call('GET',KEYS[2]) or '0')~=ARGV[3] then return 0 end; "
                               "redis.call('HSET',KEYS[1],ARGV[1],ARGV[2]); return redis.call('EXPIRE',KEYS[1],86400)";
    const auto version_key = "auth_version_" + key;
    const auto version = std::to_string(auth_version);
    auto* reply = static_cast<redisReply*>(redisCommand(
        connection,
        "EVAL %b 2 %b %b %b %b %b",
        script.data(),
        script.size(),
        key.data(),
        key.size(),
        version_key.data(),
        version_key.size(),
        field.data(),
        field.size(),
        value.data(),
        value.size(),
        version.data(),
        version.size()));
    // 释放命令回复。
    Defer free_reply([reply] {
        if (reply)
            freeReplyObject(reply);
    });
    return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
}

long long RedisMgr::EvalNumber(const std::string& script, const std::string& key, const std::string& value) {
    auto* connection = _redis_pool->GetConnection();
    if (!connection)
        throw std::runtime_error("redis unavailable");
    // 所有路径归还当前连接。
    Defer release([this, connection] {
        _redis_pool->ReturnConnection(connection);
    });
    auto* reply = static_cast<redisReply*>(redisCommand(
        connection,
        "EVAL %b 1 %b %b",
        script.data(),
        script.size(),
        key.data(),
        key.size(),
        value.data(),
        value.size()));
    // 释放可能为空的 Redis 回复。
    Defer free_reply([reply] {
        if (reply)
            freeReplyObject(reply);
    });
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
        throw std::runtime_error("redis command unavailable");
    return reply->integer;
}

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
    value.clear();
    auto connect = _redis_pool->GetConnection();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());

    if (reply == nullptr) {
        SPDLOG_ERROR("redis GET failed, key={}, reason=null_reply", key);

        freeReplyObject(reply);
        _redis_pool->ReturnConnection(connect);
        return false;
    }

    if (reply->type == REDIS_REPLY_NIL) {
        SPDLOG_DEBUG("redis GET miss, key={}", key);
        value = "";
        freeReplyObject(reply);
        _redis_pool->ReturnConnection(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        SPDLOG_WARN("redis GET failed, key={}, reason=unexpected_type, type={}", key, reply->type);
        freeReplyObject(reply);
        _redis_pool->ReturnConnection(connect);
        return false;
    }

    if (!chat_redis::ReadString(reply, value)) {
        freeReplyObject(reply);
        _redis_pool->ReturnConnection(connect);
        return false;
    }
    freeReplyObject(reply);

    SPDLOG_DEBUG("redis GET success, key={}, value_size={}", key, value.size());
    _redis_pool->ReturnConnection(connect);
    return true;
}

bool RedisMgr::HGet(const std::string& key, const std::string& hkey, std::string& value) {
    value.clear();
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

        _redis_pool->ReturnConnection(connect);
        return false;
    }

    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        SPDLOG_WARN("redis HGET failed, key={}, field={}, reason=unexpected_reply", key, hkey);
        _redis_pool->ReturnConnection(connect);
        return false;
    }
    if (!chat_redis::ReadString(reply, value)) {
        freeReplyObject(reply);
        _redis_pool->ReturnConnection(connect);
        return false;
    }

    freeReplyObject(reply);
    SPDLOG_DEBUG("redis HGET success, key={}, field={}, value_size={}", key, hkey, value.size());
    _redis_pool->ReturnConnection(connect);

    return true;
}

void RedisMgr::Close() {
    _redis_pool->Close();
}
