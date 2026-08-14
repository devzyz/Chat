#include "MysqlMgr.h"

MysqlMgr::MysqlMgr() {

}

MysqlMgr::~MysqlMgr() {

}
std::shared_ptr<UserInfo> MysqlMgr::GetUesr(int uid) {
	return _dao.GetUser(uid);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUserByName(const std::string name) {
	return _dao.GetUserByName(name);
}

bool MysqlMgr::AddFriendApply(const int& from_uid, const int& to_uid, const std::string& description, const std::string& backname) {
	return _dao.AddFriendApply(from_uid, to_uid, description, backname);
}

bool MysqlMgr::GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit) {
	return _dao.GetApplyFriendList(to_uid, applylist, start, limit);
}

bool MysqlMgr::AuthFriendApply(int apply_uid, int auth_uid, std::string auth_backname, std::string apply_backname,
	std::string apply_description, std::string auth_description, std::vector<std::shared_ptr<ChatMessage>>& chat_msgs, int& chat_id) {
	return _dao.AuthFriendApply(apply_uid, auth_uid, auth_backname, apply_backname,
		apply_description, auth_description, chat_msgs, chat_id);
}

bool MysqlMgr::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList) {
	return _dao.GetFriendList(uid, friendList);
}

bool MysqlMgr::GetUserChatList(int uid, int current_chat_id, int page_size,
	std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_load_id) {
	return _dao.GetUserChatList(uid, current_chat_id, page_size, chat_list, load_more, last_load_id);
}

bool MysqlMgr::CreatePrivateChat(int user1_id, int user2_id, int& chat_id) {
	return _dao.CreatePrivateChat(user1_id, user2_id, chat_id);
}

bool MysqlMgr::AddChatMessageList(int from_uid, int to_uid, int chat_id, std::vector<std::pair<std::string, std::string>> cache_msgs,
	std::vector<std::shared_ptr<ChatMessage>>& chat_msgs) {
	return _dao.AddChatMessageList(from_uid, to_uid, chat_id, cache_msgs, chat_msgs);
}

bool MysqlMgr::GetChatMessageList(int chat_id, int current_msg_id, int page_size,
	std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id) {
	return _dao.GetChatMessageList(chat_id, current_msg_id, page_size, chat_list, load_more, last_msg_id);
}