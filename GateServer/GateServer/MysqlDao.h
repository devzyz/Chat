#pragma once
#include "const.h"
#include "../../common/mysql/ConnectionPool.h"
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>

class SqlConnection {
public:
    explicit SqlConnection(std::unique_ptr<sql::Connection> connection = {}) : _con(std::move(connection)) {}
    std::unique_ptr<sql::Connection> _con;
};

class MysqlConnectionPool {
public:
    MysqlConnectionPool(const std::string& url, const std::string& user, const std::string& pass,
        const std::string& schema, int poolsize);
    void close();
    std::unique_ptr<SqlConnection> getConnection();
    void returnConnection(std::unique_ptr<SqlConnection> connection) noexcept;
private:
    std::unique_ptr<chat_mysql::ConnectionPool<>> _connections;
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
