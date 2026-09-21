#pragma once
#include "UserPresenceStore.h"
#include <memory>
class RedisMgr;
class RedisUserPresenceStore final : public UserPresenceStore {
public:
    RedisUserPresenceStore() = default;
    void AttachRedis(std::shared_ptr<RedisMgr> redis) { _redis = std::move(redis); }
    explicit RedisUserPresenceStore(std::shared_ptr<RedisMgr> redis) : _redis(std::move(redis)) {}
    PresenceResult Publish(int uid, const chat_session::UserPresence& presence) override;
    PresenceResult Find(int uid) override;
    bool RemoveIfCurrent(int uid, const chat_session::UserPresence& presence) override;
private:
    std::shared_ptr<RedisMgr> _redis;
};
