#pragma once
#include "const.h"
#include <thread>
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>

/**
 * 用于维持每个链接的活性, 有一个线程没隔一段时间，就检测一下每个连接上次跟sql通信过去多久了，如果超过了一定时间，此线程就通过该链接
 * 与sql通信一下，让sql知道这个链接还是有用的
 */

class SqlConnection {
public:
	/** @brief 初始化SqlConnection，持有一个 JDBC 连接及该模块所需连接状态，借用期间由调用方独占。 */
	SqlConnection(sql::Connection* con, int64_t lasttime);
	std::unique_ptr<sql::Connection> _con;
	int64_t _last_oper_time;
};

/** @brief 维护 MySQL 可用连接及关闭状态；所有借出连接须在池析构之前归还。 */
class MysqlConnectionPool {
public:
	/** @brief 初始化MysqlConnectionPool，维护 MySQL 可用连接及关闭状态；所有借出连接须在池析构之前归还。 */
	MysqlConnectionPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolsize);
	/** @brief 在锁内遍历空闲连接，对超过保活间隔的连接执行查询并更新时间。 */
	void CheckConnection();
	/** @brief 仅清空空闲连接；当前实现不停止或 join 保活线程。 */
	~MysqlConnectionPool();
	/** @brief 设置停止标志并唤醒借用等待者，不清空空闲队列，也不等待保活线程退出。 */
	void Close();
	/** @brief 等待空闲连接或关闭；耗尽时无超时，关闭返回空值，借出前不另做健康检查。 */
	std::unique_ptr<SqlConnection> GetConnection();
	/** @brief 把传入连接放回队列并唤醒等待者，池关闭时释放；不执行健康或事务校验。 */
	void ReturnConnection(std::unique_ptr<SqlConnection> con);
private:
	std::string _url;
	std::string _user;
	std::string _pass;
	std::string _schema;
	int _poolSize; // 连接池大小
	std::queue<std::unique_ptr<SqlConnection>> _pool; // 连接池
	std::mutex _mutex;
	std::condition_variable _cond;
	std::atomic<bool> _b_stop;
	std::thread _check_thread;
};

/** @brief 保存查询得到的用户资料；仅作为进程内数据对象，不控制会话生命周期。 */
struct UserInfo {
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
};

/** @brief 以连接池执行用户资料与认证数据操作，失败通过返回值或已声明异常路径传递。 */
class MysqlDao {
public:
	/** @brief 初始化MysqlDao，以连接池执行用户资料与认证数据操作，失败通过返回值或已声明异常路径传递。 */
	MysqlDao();
	/** @brief 释放数据库访问对象及其持有的连接池。 */
	~MysqlDao();
	/** @brief 注册用户资料并返回分配的 UID；失败由约定的错误返回值表示。 */
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	/** @brief 核对用户名与邮箱是否属于同一用户，返回校验结果。 */
	bool CheckEmail(const std::string& username, const std::string& email);
	/** @brief 更新指定用户密码并返回数据库操作结果，不在此处完成验证码校验。 */
	bool UpdatePassword(const std::string& username, const std::string& password);
	/** @brief 按邮箱核验密码，成功时填充用户资料，失败结果不得作为已认证身份使用。 */
	bool CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo);
private:
	std::unique_ptr<MysqlConnectionPool> _pool;
};
