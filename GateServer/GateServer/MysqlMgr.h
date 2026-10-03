#pragma once
#include "const.h"
#include "MysqlDao.h"
/** @brief 提供进程内 DAO 访问入口并转发数据库业务操作，不代表跨服务事务。 */
class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	/** @brief 释放数据库访问对象及其持有的连接池。 */
	~MysqlMgr();
	/** @brief 注册用户资料并返回分配的 UID；失败由约定的错误返回值表示。 */
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	/** @brief 核对用户名与邮箱是否属于同一用户，返回校验结果。 */
	bool CheckEmail(const std::string& username, const std::string& email);
    /** @brief 账号锁内撤销凭据再更新密码，传播存储或撤销失败。 */
    bool UpdatePassword(const std::string& username, const std::string& password, const std::string& email,
        const std::function<bool(int)>& revoke);
    /** @brief 核验凭据并返回持有账号锁的资料；调用方保持租约直至发布 Token 结束。 */
	bool CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo);
private:
	/** @brief 初始化MysqlMgr，提供进程内 DAO 访问入口并转发数据库业务操作，不代表跨服务事务。 */
	MysqlMgr();
	MysqlDao _dao;
};

