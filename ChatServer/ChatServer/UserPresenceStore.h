#pragma once
#include "SessionTypes.h"
#include <optional>

namespace chat_session {
struct UserPresence { std::string server_id; SessionId session_id; };
}
enum class PresenceStatus { Found, NotFound, Unavailable };
struct PresenceResult {
    PresenceStatus status = PresenceStatus::Unavailable;
    std::optional<chat_session::UserPresence> presence;
};
class UserPresenceStore {
public:
    virtual ~UserPresenceStore() = default;
    // Successful publication returns the previous presence (or NotFound).
    virtual PresenceResult Publish(int uid, const chat_session::UserPresence& presence) = 0;
    virtual PresenceResult Find(int uid) = 0;
    // false denotes storage failure; a non-matching owner is a successful no-op.
    virtual bool RemoveIfCurrent(int uid, const chat_session::UserPresence& presence) = 0;
};
