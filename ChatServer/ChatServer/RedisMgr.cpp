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

void RedisMgr::Close() {
    _pool->Close();
}

std::optional<std::vector<std::string>> RedisMgr::Eval(
    const std::string& script,
    const std::vector<std::string>& keys,
    const std::vector<std::string>& arguments) {
    auto* connection = CheckedConnectionPool().GetConnection();
    if (!connection)
        return std::nullopt;
    // 归还本次借用的 Redis 连接，退出作用域后不得再使用。
    Defer release([this, connection] {
        _pool->ReturnConnection(connection);
    });
    const timeval timeout{2, 0};
    if (redisSetTimeout(connection, timeout) != REDIS_OK)
        return std::nullopt;
    std::vector<std::string> command{"EVAL", script, std::to_string(keys.size())};
    command.insert(command.end(), keys.begin(), keys.end());
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::vector<const char*> argv;
    std::vector<std::size_t> lengths;
    for (const auto& value : command) {
        argv.push_back(value.data());
        lengths.push_back(value.size());
    }
    auto* reply = static_cast<redisReply*>(
        redisCommandArgv(connection, static_cast<int>(argv.size()), argv.data(), lengths.data()));
    if (!reply)
        return std::nullopt;
    // 释放本次 Redis 命令响应，保证异常路径也回收内存。
    Defer free_reply([reply] {
        freeReplyObject(reply);
    });
    if (reply->type != REDIS_REPLY_ARRAY)
        return std::nullopt;
    std::vector<std::string> result;
    for (std::size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type != REDIS_REPLY_STRING)
            return std::nullopt;
        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
    }
    return result;
}
