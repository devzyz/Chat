#pragma once

#include <hiredis/hiredis.h>
#include <string>

namespace chat_redis {
inline bool IsPositiveInteger(const redisReply* reply) {
    return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
}

inline bool ReadString(const redisReply* reply, std::string& value) {
    value.clear();
    if (!reply || reply->type != REDIS_REPLY_STRING || !reply->str) return false;
    value.assign(reply->str, reply->len);
    return true;
}
} // namespace chat_redis
