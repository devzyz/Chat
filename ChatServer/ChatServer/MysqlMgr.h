#pragma once
#include "Singleton.h"
#include "MysqlDao.h"
#include "const.h"
#include <iostream>
#include "data.h"
#include <memory>

class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	std::shared_ptr<UserInfo>  GetUesr(int uid);
private:
	MysqlMgr();
	MysqlDao _dao;
};

