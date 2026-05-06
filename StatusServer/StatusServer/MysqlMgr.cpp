#include "MysqlMgr.h"

MysqlMgr::~MysqlMgr() {

}

MysqlMgr::MysqlMgr() {

}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
	return _dao.RegUser(name, email, pwd);
}

bool MysqlMgr::CheckEmail(const std::string& username, const std::string& email) {
	return _dao.CheckEmail(username, email);
}

bool MysqlMgr::UpdatePassword(const std::string& username, const std::string& password) {
	return _dao.UpdatePassword(username, password);
}

bool MysqlMgr::CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo) {
	return _dao.CheckPassword(email, password, userinfo);
}
