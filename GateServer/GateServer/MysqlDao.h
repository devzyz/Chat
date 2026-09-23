#pragma once
#include "const.h"
#include "../../common/mysql/ConnectionPool.h"
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>

/** @brief 持有一个 JDBC 连接及该模块所需连接状态，借用期间由调用方独占。 */
class SqlConnection {
public:
    /** @brief 初始化SqlConnection，持有一个 JDBC 连接及该模块所需连接状态，借用期间由调用方独占。 */
    explicit SqlConnection(std::unique_ptr<sql::Connection> connection = {}) : _con(std::move(connection)) {}
    std::unique_ptr<sql::Connection> _con;
};

/** @brief 维护 MySQL 可用连接及关闭状态；所有借出连接须在池析构之前归还。 */
class MysqlConnectionPool {
public:
    /** @brief 初始化MysqlConnectionPool，维护 MySQL 可用连接及关闭状态；所有借出连接须在池析构之前归还。 */
    MysqlConnectionPool(const std::string& url, const std::string& user, const std::string& pass,
        const std::string& schema, int poolsize);
    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close();
    /** @brief 在池的等待策略内借出独占连接；关闭、耗尽或健康检查失败返回空值，使用后须归还。 */
    std::unique_ptr<SqlConnection> GetConnection();
    /** @brief 接回借出的连接所有权，关闭或不可用连接由底层池丢弃；归还后不得继续访问。 */
    void ReturnConnection(std::unique_ptr<SqlConnection> connection) noexcept;
private:
    std::unique_ptr<chat_mysql::ConnectionPool<>> _connections;
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
