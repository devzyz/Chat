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

bool MysqlMgr::AddFriendApply(const int& from_uid, const int& to_uid) {
	return _dao.AddFriendApply(from_uid, to_uid);
}

bool MysqlMgr::GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit) {
	return _dao.GetApplyFriendList(to_uid, applylist, start, limit);
}

bool MysqlMgr::UpdateFriendAuth(int fromuid, int touid, std::string backname) {
	return _dao.UpdateFriendAuth(fromuid, touid, backname);
}

bool MysqlMgr::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList) {
	return _dao.GetFriendList(uid, friendList);
}