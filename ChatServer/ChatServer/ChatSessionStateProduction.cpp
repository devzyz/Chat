#include "ChatSessionStateProduction.h"

#include "ChatSessionState.h"
#include "ChatSessionStateInternal.h"
#include "Const.h"
#include "RedisMgr.h"

#include <boost/uuid.hpp>

namespace {

class UuidSessionIdSource final : public SessionIdSource {
public:
	std::string Next() override {
		return boost::uuids::to_string(boost::uuids::random_generator()());
	}
};

class RedisSessionPresence final : public SessionPresence {
public:
	void Register(int uid, const std::string& session_id) override {
		RedisMgr::GetInstance()->Set(USER_SESSION_KEY + std::to_string(uid), session_id);
	}

	void Cleanup(int uid, const std::string&) override {
		const auto uid_text = std::to_string(uid);
		const auto lock_key = LOCK_PREFIX + uid_text;
		const auto identifier = RedisMgr::GetInstance()->acquireLock(
			lock_key, LOCK_TIME_OUT, LOCK_ACQUIRE_TIME_OUT);
		if (identifier.empty()) {
			return;
		}
		Defer release([identifier, lock_key]() {
			RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

		const auto session_key = USER_SESSION_KEY + uid_text;
		RedisMgr::GetInstance()->Del(session_key);
		RedisMgr::GetInstance()->Del(USER_IP_PREFIX + uid_text);
	}
};

} // namespace

std::shared_ptr<ChatSessionState> MakeProductionChatSessionState() {
	return std::make_shared<ChatSessionState>(
		std::make_shared<UuidSessionIdSource>(),
		std::make_shared<RedisSessionPresence>());
}
