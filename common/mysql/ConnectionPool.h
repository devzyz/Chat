#pragma once
#include "../database/ConnectionPool.h"
#include <jdbc/cppconn/connection.h>

namespace chat_mysql {
// 为已有 JDBC 连接和生命周期夹具适配借还检查。
struct JdbcPolicy {
    // 从空闲队列借出时确认连接仍可执行 SQL。
    template <class T> static bool Healthy(T& db) {
        return db.isValid();
    }

    // 归还前恢复自动提交；返回 false 或抛异常均会丢弃连接。
    template <class T> static bool Reset(T& db) {
        if (db.isClosed()) return false;
        if (!db.getAutoCommit()) {
            db.rollback();
            db.setAutoCommit(true);
        }
        return true;
    }
};
// 保留既有 JDBC 调用方的默认驱动和借还合同。
template <class Connection = sql::Connection> using ConnectionPool = database::ConnectionPool<Connection, JdbcPolicy>;
} // namespace chat_mysql
