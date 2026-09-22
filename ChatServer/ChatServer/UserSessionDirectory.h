#pragma once
#include "SessionTypes.h"
#include <memory>
#include <mutex>
#include <unordered_map>

class CSession;
class UserSessionDirectory {
public:
    std::shared_ptr<CSession> Register(int uid, const SessionId& id, std::weak_ptr<CSession> session);
    std::shared_ptr<CSession> FindCurrent(int uid) const;
    bool IsCurrent(int uid, const SessionId& id) const;
    void UnregisterIfCurrent(int uid, const SessionId& id);
private:
    struct Entry { SessionId id; std::weak_ptr<CSession> session; };
    mutable std::mutex _mutex;
    std::unordered_map<int, Entry> _entries;
};
