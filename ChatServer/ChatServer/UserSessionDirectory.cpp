#include "UserSessionDirectory.h"

std::shared_ptr<CSession> UserSessionDirectory::Register(int uid, const SessionId& id,
    std::weak_ptr<CSession> session) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto old = _entries[uid].session.lock();
    _entries[uid] = {id, std::move(session)};
    return old;
}
std::shared_ptr<CSession> UserSessionDirectory::FindCurrent(int uid) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _entries.find(uid);
    return it == _entries.end() ? nullptr : it->second.session.lock();
}
bool UserSessionDirectory::IsCurrent(int uid, const SessionId& id) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _entries.find(uid);
    return it != _entries.end() && it->second.id == id && !it->second.session.expired();
}
void UserSessionDirectory::UnregisterIfCurrent(int uid, const SessionId& id) {
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _entries.find(uid);
    if (it != _entries.end() && it->second.id == id) _entries.erase(it);
}
