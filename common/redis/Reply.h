#pragma once

#include <hiredis/hiredis.h>
#include <string>

namespace chat_redis {
/** @brief 判断 Redis 响应是否为有效的正整数结果。 */
inline bool IsPositiveInteger(const redisReply* reply) {
    return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
}

/** @brief 核验 Redis 响应类型并复制字符串字节至输出参数，失败返回 false。 */
inline bool ReadString(const redisReply* reply, std::string& value) {
    value.clear();
    if (!reply || reply->type != REDIS_REPLY_STRING || !reply->str) return false;
    value.assign(reply->str, reply->len);
    return true;
}
} // namespace chat_redis
