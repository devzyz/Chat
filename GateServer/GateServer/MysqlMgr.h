#pragma once
#include "const.h"
#include "MysqlDao.h"
class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	bool CheckEmail(const std::string& username, const std::string& email);
	bool UpdatePassword(const std::string& username, const std::string& password);
	bool CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo);
private:
	MysqlMgr();
	MysqlDao _dao;
};

