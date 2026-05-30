#pragma once
#include "Singleton.h"
#include "MysqlDao.h"
#include "const.h"
#include <iostream>
#include "Data.h"
#include <memory>

class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	// 根据uid查询用户详细信息
	std::shared_ptr<UserInfo> GetUesr(int uid);
	// 根据name查询用户详细信息
	std::shared_ptr<UserInfo> GetUserByName(const std::string name);
	// 往好友申请表中插入数据
	bool AddFriendApply(const int& from_uid, const int& to_uid);
	// 读取申请添加to_uid为好友的用户信息列表
	bool GetApplyFriendList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist, int start, int limit);
	// 更新好友申请表和好友表
	bool UpdateFriendAuth(int fromuid, int touid, std::string backname);
	// 获取用户好友列表
	bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friendList);
private:
	MysqlMgr();
	MysqlDao _dao;
};

