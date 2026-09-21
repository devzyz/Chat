#include "RedisUserPresenceStore.h"
#include "RedisMgr.h"
#include "Const.h"

namespace {
std::vector<std::string> Keys(int uid) {
    return {USER_IP_PREFIX + std::to_string(uid), USER_SESSION_KEY + std::to_string(uid)};
}
PresenceResult Decode(const std::optional<std::vector<std::string>>& result) {
    if (!result || result->size() != 2) return {};
    if ((*result)[0].empty() || (*result)[1].empty()) return {PresenceStatus::NotFound, {}};
    return {PresenceStatus::Found, chat_session::UserPresence{(*result)[0], (*result)[1]}};
}
}
PresenceResult RedisUserPresenceStore::Publish(int uid, const chat_session::UserPresence& presence) {
    return Decode(_redis->Eval(
        "local old={redis.call('GET',KEYS[1]) or '',redis.call('GET',KEYS[2]) or ''}; "
        "redis.call('MSET',KEYS[1],ARGV[1],KEYS[2],ARGV[2]); return old",
        Keys(uid), {presence.server_id, presence.session_id}));
}
PresenceResult RedisUserPresenceStore::Find(int uid) {
    return Decode(_redis->Eval(
        "return {redis.call('GET',KEYS[1]) or '',redis.call('GET',KEYS[2]) or ''}", Keys(uid), {}));
}
bool RedisUserPresenceStore::RemoveIfCurrent(int uid, const chat_session::UserPresence& presence) {
    return _redis->Eval(
        "if redis.call('GET',KEYS[1])==ARGV[1] and redis.call('GET',KEYS[2])==ARGV[2] then "
        "redis.call('DEL',KEYS[1],KEYS[2]); end; return {}",
        Keys(uid), {presence.server_id, presence.session_id}).has_value();
}
