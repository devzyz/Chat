#pragma once
#include "UserPresenceStore.h"
#include <memory>
class RedisMgr;
/** @brief 通过 Redis Lua 原子维护实例和会话双键；使用前必须注入有效 RedisMgr。 */
class RedisUserPresenceStore final : public UserPresenceStore {
public:
    /** @brief 创建待装配适配器，调用存储方法前必须 AttachRedis。 */
    RedisUserPresenceStore() = default;
    /** @brief 在首次使用前注入共享 Redis 依赖，不得与存储调用并发执行。 */
    void AttachRedis(std::shared_ptr<RedisMgr> redis) { _redis = std::move(redis); }
    /** @brief 创建持有有效 Redis 依赖的适配器。 */
    explicit RedisUserPresenceStore(std::shared_ptr<RedisMgr> redis) : _redis(std::move(redis)) {}
    /** @brief 原子覆盖双键并返回旧位置；无完整旧值为 NotFound，Eval 失败为 Unavailable。 */
    PresenceResult Publish(int uid, const chat_session::UserPresence& presence) override;
    /** @brief 原子读取双键；任一值为空视为 NotFound，Eval 失败为 Unavailable。 */
    PresenceResult Find(int uid) override;
    /** @brief 仅在双键匹配时删除；无匹配也成功，Eval 失败返回 false。 */
    bool RemoveIfCurrent(int uid, const chat_session::UserPresence& presence) override;
private:
    std::shared_ptr<RedisMgr> _redis;
};
