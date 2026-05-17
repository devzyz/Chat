#pragma once
#include "const.h"
#include <iostream>
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include <queue>
#include <mutex>
#include "data.h"
#include <memory>

/**
 * @brief 
 * sql的每个连接包含的信息，一个是sql连接指针，另一个是最后操作时间
 * 因为sql长时间不操作，连接可能会被断开，可以通过检查最后操作时间来实现心跳函数，让sql连接保持存活
 */
class SQLConnection {
public:
	SQLConnection(sql::Connection* connection, int64_t lasttime);
	std::unique_ptr<sql::Connection> _connection;
	int64_t _last_operator_time;
};

/**
 * @brief 
 * sql的连接池
 */
class MysqlPool {
public:
	MysqlPool(const std::string& url, const std::string& user, const std::string& password, const std::string& schema,
		int poolSize);
	~MysqlPool();

	std::unique_ptr<SQLConnection> GetConnection();
	void returnConnection(std::unique_ptr<SQLConnection> connection);
	void close();
private:
	void CheckConnection();
	// 连接池队列，以及互斥访问连接池的信号
	std::mutex _que_mutex;
	std::queue<std::unique_ptr<SQLConnection>> _que;

	std::atomic<bool> _b_stop;

	// 当连接池为空的时候，需要通过条件变量释放锁并等待
	std::condition_variable _cond;

	// 连接需要的数据
	int _pool_size;
	std::string _url; // 数据库地址
	std::string _user; // 用户名
	std::string _password; // 密码
	std::string _schema; // 分组

	// 心跳检查程序
	std::thread _check_thread;
};

class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();

	std::shared_ptr<UserInfo> GetUser(int uid);
private:
	std::unique_ptr<MysqlPool> _pool;
};

