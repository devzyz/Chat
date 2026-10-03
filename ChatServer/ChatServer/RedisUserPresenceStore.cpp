#include "RedisUserPresenceStore.h"
#include "RedisMgr.h"
#include "Const.h"

namespace {
/** @brief 按固定顺序构造实例、会话、账号 Token 与会话绑定 Token 键，与 Lua 索引对应。 */
std::vector<std::string> Keys(int uid) {
    const auto id = std::to_string(uid);
    return {USER_IP_PREFIX + id, USER_SESSION_KEY + id, id, "usessiontoken_" + id};
}
/** @brief 将双键响应映射为位置结果；读取失败或元素数异常为不可用，空字段为不存在。 */
PresenceResult Decode(const std::optional<std::vector<std::string>>& result) {
    if (!result || result->size() != 2) return {};
    if ((*result)[0].empty() || (*result)[1].empty()) return {PresenceStatus::NotFound, {}};
    return {PresenceStatus::Found, chat_session::UserPresence{(*result)[0], (*result)[1]}};
}
}
PresenceResult RedisUserPresenceStore::Publish(int uid, const chat_session::UserPresence& presence) {
    if (presence.authentication_token.empty()) return {};
    return Decode(_redis->Eval(
        "if redis.call('HGET',KEYS[3],'utoken_'..KEYS[3])~=ARGV[3] then return {} end; "
        "local old={redis.call('GET',KEYS[1]) or '',redis.call('GET',KEYS[2]) or ''}; "
        "redis.call('MSET',KEYS[1],ARGV[1],KEYS[2],ARGV[2],KEYS[4],ARGV[3]); "
        "redis.call('EXPIRE',KEYS[1],90); redis.call('EXPIRE',KEYS[2],90); redis.call('EXPIRE',KEYS[4],90); return old",
        Keys(uid), {presence.server_id, presence.session_id, presence.authentication_token}));
}
PresenceResult RedisUserPresenceStore::Find(int uid) {
    return Decode(_redis->Eval(
        "return {redis.call('GET',KEYS[1]) or '',redis.call('GET',KEYS[2]) or ''}", Keys(uid), {}));
}
bool RedisUserPresenceStore::RefreshIfCurrent(int uid, const SessionId& session_id) {
    const auto result = _redis->Eval(
        "if redis.call('GET',KEYS[2])~=ARGV[1] or not redis.call('GET',KEYS[1]) then return {} end; "
        "local bound=redis.call('GET',KEYS[4]); if not bound or redis.call('HGET',KEYS[3],'utoken_'..KEYS[3])~=bound then return {} end; "
        "redis.call('EXPIRE',KEYS[1],90); redis.call('EXPIRE',KEYS[2],90); redis.call('EXPIRE',KEYS[4],90); "
        "redis.call('EXPIRE',KEYS[3],86400); return {'current'}",
        Keys(uid), {session_id});
    return result && result->size() == 1 && (*result)[0] == "current";
}
bool RedisUserPresenceStore::RemoveIfCurrent(int uid, const chat_session::UserPresence& presence) {
    return _redis->Eval(
        "if redis.call('GET',KEYS[1])==ARGV[1] and redis.call('GET',KEYS[2])==ARGV[2] then "
        "redis.call('DEL',KEYS[1],KEYS[2],KEYS[4]); end; return {}",
        Keys(uid), {presence.server_id, presence.session_id}).has_value();
}
