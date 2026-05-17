#include "MysqlMgr.h"

MysqlMgr::MysqlMgr() {

}

MysqlMgr::~MysqlMgr() {

}
std::shared_ptr<UserInfo> MysqlMgr::GetUesr(int uid) {
	return _dao.GetUser(uid);
}