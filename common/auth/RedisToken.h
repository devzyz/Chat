#pragma once

#include "../redis/RedisPool.h"
#include <memory>
#include <string>

namespace chat_auth {
/** @brief 描述凭据读取结果；各协议自行映射业务错误码。 */
enum class TokenResult { Valid, MissingUser, Invalid, Unavailable };

/** @brief 使用有界连接池读取当前凭据；不修改 TTL 或会话信息。 */
inline TokenResult ValidateToken(chat_redis::RedisPool& pool, int uid, const std::string& token) {
    if (uid <= 0)
        return TokenResult::MissingUser;
    if (token.empty())
        return TokenResult::Invalid;
    const auto key = std::to_string(uid);
    const auto field = "utoken_" + key;
    // 离开校验作用域时把独占连接归还给原池。
    auto release = [&pool](redisContext* connection) {
        pool.Return(connection);
    };
    std::unique_ptr<redisContext, decltype(release)> connection(pool.Borrow(), release);
    if (!connection)
        return TokenResult::Unavailable;
    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(connection.get(), "HGET %s %s", key.c_str(), field.c_str())),
        &freeReplyObject);
    if (!reply)
        return TokenResult::Unavailable;
    if (reply->type == REDIS_REPLY_NIL)
        return TokenResult::MissingUser;
    if (reply->type != REDIS_REPLY_STRING || !reply->str)
        return TokenResult::Unavailable;
    return token.compare(0, token.size(), reply->str, reply->len) == 0 ? TokenResult::Valid : TokenResult::Invalid;
}
} // namespace chat_auth
