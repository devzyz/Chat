#include "UserMgr.h"
#include "CSession.h"

UserMgr::~UserMgr() {
	_uid_to_session.clear();
}

std::shared_ptr<CSession> UserMgr::GetSession(int uid) {
	std::lock_guard<std::mutex> lock(_session_mutex);
	auto iter = _uid_to_session.find(uid);
	if (iter == _uid_to_session.end()) {
		return nullptr;
	}

	return iter->second;
}

void UserMgr::SetUserSession(int uid, std::shared_ptr<CSession> session) {
	std::lock_guard<std::mutex> lock(_session_mutex);
	_uid_to_session[uid] = session;
}

// 每一个session，都对应一个session_id
// 因为有分布式锁的存在，所以可能再同账号再次登录到同一个服务器，可能会先持有锁，此时将session_id已经改为最新的了
// 此时无需进行删除了
// 如果是旧的session先关闭，则需要删除
// 注意，不管那种情况的删除，session都不会真正的析构，因为在接收客户端发送信息的回调逻辑中，还保留着session，只有这个被析构了
// 此时session才会真正的被析构
// 这里的now_session_id通过redis来存储
void UserMgr::RemoveUserSession(int uid, std::string session_id) {
	{
		std::lock_guard<std::mutex> lock(_session_mutex);
		
		// 如果找不到uid对应的连接，则默认删除完成，直接返回
		auto iter = _uid_to_session.find(uid);
		if (iter == _uid_to_session.end()) {
			return;
		}

		// 由传过来的session的sessionId，与最新的session_id进行匹配，如果不相等，代表已经是新的连接了，直接返回，否则代表是旧连接，删除旧连接
		auto now_session_id = iter->second->GetSessionId();
		if (now_session_id != session_id) {
			return;
		}

		_uid_to_session.erase(uid);
	}
}

UserMgr::UserMgr() {

}
