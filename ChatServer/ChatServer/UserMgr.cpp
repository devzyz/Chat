#include "UserMgr.h"
#include "ChatSessionStateProduction.h"

UserMgr::~UserMgr() = default;

std::shared_ptr<ChatSessionState> UserMgr::Sessions() const {
	return _sessions;
}

UserMgr::UserMgr() : _sessions(MakeProductionChatSessionState()) {}
