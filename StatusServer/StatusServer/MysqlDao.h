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
	SqlConnection(sql::Connection* con, int64_t lasttime);
	std::unique_ptr<sql::Connection> _con;
	int64_t _last_oper_time;
};

class MysqlConnectionPool {
public:
	MysqlConnectionPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolsize);
	void checkConnection();
	~MysqlConnectionPool();
	void close();
	std::unique_ptr<SqlConnection> getConnection();
	void returnConnection(std::unique_ptr<SqlConnection> con);
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

struct UserInfo {
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
};

class MysqlDao {
public:
	MysqlDao();
	~MysqlDao();
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	bool CheckEmail(const std::string& username, const std::string& email);
	bool UpdatePassword(const std::string& username, const std::string& password);
	bool CheckPassword(const std::string& email, const std::string& password, UserInfo& userinfo);
private:
	std::unique_ptr<MysqlConnectionPool> _pool;
};

