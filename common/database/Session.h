#pragma once

#include "../postgres/Connection.h"
#include "../postgres/Verify.h"
#include "../../schema/SchemaContract.h"
#include <jdbc/cppconn/prepared_statement.h>
#include <charconv>
#include <cstdint>
#include <limits>

namespace database {
// 双方可共享标准 SQL；需要特定语法时在操作定义处显式指定两条语句。
struct QueryText {
    // 保存当前语句在两种驱动下的参数与 SQL 文本。
    QueryText(const char* shared) : mysql(shared), postgres(shared) {}

    // 保存当前语句在两种驱动下的参数与 SQL 文本。
    QueryText(const std::string& shared) : mysql(shared), postgres(shared) {}

    // 保存当前语句在两种驱动下的参数与 SQL 文本。
    QueryText(std::string source, std::string target) : mysql(std::move(source)), postgres(std::move(target)) {}

    std::string mysql, postgres;
};

// 按字段名或从 1 开始的列号只读当前行；PG bytea 返回原始字节。
class ResultSet final {
public:
    // 接管原生查询结果，由结果对象负责释放。
    explicit ResultSet(sql::ResultSet* rows) : mysql_(rows) {}

    // 接管原生查询结果，由结果对象负责释放。
    explicit ResultSet(chat_pg::Rows rows) : postgres_(std::move(rows)) {}

    // 移动到下一行，耗尽时返回 false。
    bool Next() {
        return mysql_ ? mysql_->next() : ++row_ < PQntuples(postgres_.get());
    }

    // 按名称或列号读取原值，二进制列保留零字节。
    template <class Column> std::string GetString(Column col) const {
        if (mysql_) return mysql_->getString(col).asStdString();
        const int i = Index(col);
        if (PQgetisnull(postgres_.get(), row_, i)) return {};
        const char* value = PQgetvalue(postgres_.get(), row_, i);
        if (PQftype(postgres_.get(), i) != 17) return value;
        std::size_t size;
        std::unique_ptr<unsigned char, decltype(&PQfreemem)> bytes(
            PQunescapeBytea(reinterpret_cast<const unsigned char*>(value), &size), PQfreemem);
        if (!bytes) throw chat_pg::Error("22000");
        return {reinterpret_cast<const char*>(bytes.get()), size};
    }

    // 读取可表示为 int 的数值列。
    template <class Column> int GetInt(Column col) const {
        return mysql_ ? mysql_->getInt(col) : Number<int>(col);
    }

    // 读取有符号 64 位整数。
    template <class Column> std::int64_t GetInt64(Column col) const {
        return mysql_ ? mysql_->getInt64(col) : Number<std::int64_t>(col);
    }

    // 读取无符号 64 位整数。
    template <class Column> std::uint64_t GetUInt64(Column col) const {
        return mysql_ ? mysql_->getUInt64(col) : Number<std::uint64_t>(col);
    }

    // 将驱动的布尔表示还原为业务标志。
    template <class Column> bool GetBoolean(Column col) const {
        if (mysql_) return mysql_->getBoolean(col);
        const auto value = GetString(col);
        return value == "t" || value == "1";
    }

    // 区分数据库 NULL 与零或空串。
    template <class Column> bool IsNull(Column col) const {
        return mysql_ ? mysql_->isNull(col) : PQgetisnull(postgres_.get(), row_, Index(col)) != 0;
    }

private:
    // 按目标宽度严格解析 PG 文本格式的数值。
    template <class T, class Column> T Number(Column col) const {
        if (IsNull(col)) return 0;
        const auto text = GetString(col);
        T value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
            throw std::range_error("DatabaseIntegerRange");
        }
        return value;
    }

    // 验证列标识并转换成 libpq 列号。
    int Index(int col) const {
        if (row_ < 0 || row_ >= PQntuples(postgres_.get()) || col < 1 || col > PQnfields(postgres_.get())) {
            throw std::out_of_range("DatabaseColumn");
        }
        return col - 1;
    }

    // 验证列标识并转换成 libpq 列号。
    int Index(const char* col) const {
        return Index(PQfnumber(postgres_.get(), col) + 1);
    }

    std::unique_ptr<sql::ResultSet> mysql_;
    chat_pg::Rows postgres_{nullptr, PQclear};
    int row_ = -1;
};

// 非拥有会话视图，允许同一业务事务在驱动之间共享判断和锁顺序。
class PreparedStatement;
class Statement;

// 借用驱动连接，生命周期由调用方负责。
class Session {
public:
    // 借用驱动连接，生命周期由调用方负责。
    Session(sql::Connection& db) : mysql_(&db) {}

    // 借用驱动连接，生命周期由调用方负责。
    Session(chat_pg::Connection& db) : postgres_(&db) {}

    // 返回当前会话借用的 MySQL 连接，其他后端返回空。
    sql::Connection* Mysql() const {
        return mysql_;
    }

    // 返回当前会话借用的 PG 连接，其他后端返回空。
    chat_pg::Connection* Postgres() const {
        return postgres_;
    }

    // 为当前物理连接选择方言并创建待绑定的语句。
    PreparedStatement* PrepareStatement(const QueryText& text) const;
    // 创建无参数语句，由调用方管理其生命周期。
    Statement* CreateStatement() const;

    // 检查物理连接能否继续归还连接池。
    bool IsClosed() const {
        return mysql_ ? mysql_->isClosed() : postgres_->IsClosed();
    }

    // 借出前通过驱动验证实际连接。
    bool IsValid() {
        return mysql_ ? mysql_->isValid() : postgres_->IsValid();
    }

    // 判断当前连接是否在显式事务之外。
    bool GetAutoCommit() const {
        return mysql_ ? mysql_->getAutoCommit() : postgres_->GetAutoCommit();
    }

    // 开始事务或在回滚后确认连接已经复位。
    void SetAutoCommit(bool enabled) {
        if (mysql_) {
            mysql_->setAutoCommit(enabled);
        } else if (!enabled) {
            postgres_->Query("BEGIN ISOLATION LEVEL READ COMMITTED");
        } else {
            postgres_->SetAutoCommit(true);
        }
    }

    // 提交当前事务并保留驱动的错误分类。
    void Commit() {
        if (mysql_) {
            mysql_->commit();
        } else {
            postgres_->Query("COMMIT");
        }
    }

    // 撤销当前连接上尚未提交的事务。
    void Rollback() {
        if (mysql_) {
            mysql_->rollback();
        } else {
            postgres_->Rollback();
        }
    }

    // 主动断开连接，同时释放服务端会话锁。
    void Close() {
        if (mysql_) {
            mysql_->close();
        } else {
            postgres_->Close();
        }
    }

    // 在监听前校验当前驱动的数据库结构和版本。
    void Verify() {
        if (mysql_) {
            chat_schema::Verify(*mysql_);
        } else {
            chat_pg::Verify(*postgres_);
        }
    }

private:
    sql::Connection* mysql_ = nullptr;
    chat_pg::Connection* postgres_ = nullptr;
};

// 连接池拥有物理连接，每个借用者只持有会话视图。
class Owned final : public Session {
public:
    // 接管独占物理连接，作为池中的所有权单元。
    explicit Owned(std::unique_ptr<sql::Connection> db) : Session(*db), mysql_(std::move(db)) {}

    // 接管独占物理连接，作为池中的所有权单元。
    explicit Owned(std::unique_ptr<chat_pg::Connection> db) : Session(*db), postgres_(std::move(db)) {}

private:
    std::unique_ptr<sql::Connection> mysql_;
    std::unique_ptr<chat_pg::Connection> postgres_;
};

// 只处理仓库内的固定 SQL。PG 方言用 ? 绑定文本，用 ?b 绑定原始字节。
class PreparedStatement final {
public:
    // 创建驱动语句，PG 参数按声明的位置绑定。
    PreparedStatement(Session db, const QueryText& sql) : db_(db) {
        if (db.Mysql()) {
            mysql_.reset(db.Mysql()->prepareStatement(sql.mysql));
            return;
        }
        for (std::size_t i = 0; i < sql.postgres.size(); ++i) {
            if (sql.postgres[i] != '?') {
                text_ += sql.postgres[i];
                continue;
            }
            const bool binary = i + 1 < sql.postgres.size() && sql.postgres[i + 1] == 'b';
            binary_.push_back(binary);
            const auto parameter = "$" + std::to_string(binary_.size());
            text_ += binary ? "decode(" + parameter + ",'hex')" : parameter;
            if (binary) ++i;
        }
        args_.resize(binary_.size());
    }

    // 绑定文本参数，对字节列按长度编码。
    void SetString(int i, const std::string& value) {
        if (mysql_) {
            mysql_->setString(i, value);
        } else {
            args_.at(i - 1) = binary_.at(i - 1) ? chat_pg::Hex(value) : value;
        }
    }

    // 绑定有符号整数参数。
    void SetInt(int i, int value) {
        if (mysql_) {
            mysql_->setInt(i, value);
        } else {
            SetString(i, std::to_string(value));
        }
    }

    // 绑定有符号 64 位参数。
    void SetInt64(int i, std::int64_t v) {
        if (mysql_) {
            mysql_->setInt64(i, v);
        } else {
            SetString(i, std::to_string(v));
        }
    }

    // 绑定非负 64 位参数。
    void SetUInt64(int i, std::uint64_t v) {
        if (mysql_) {
            mysql_->setUInt64(i, v);
        } else {
            SetString(i, std::to_string(v));
        }
    }

    // 执行读取，返回由调用方持有的结果集。
    ResultSet* ExecuteQuery() {
        return mysql_ ? new ResultSet(mysql_->executeQuery())
                      : new ResultSet(db_.Postgres()->Query(text_.c_str(), args_));
    }

    // 执行写语句并返回受影响的记录数。
    int ExecuteUpdate() {
        if (mysql_) return mysql_->executeUpdate();
        auto rows = db_.Postgres()->Query(text_.c_str(), args_);
        const char* count = PQcmdTuples(rows.get());
        return *count ? std::stoi(count) : 0;
    }

    // 执行无需消费结果集的语句。
    bool Execute() {
        if (mysql_) return mysql_->execute();
        ExecuteUpdate();
        return false;
    }

private:
    Session db_;
    std::unique_ptr<sql::PreparedStatement> mysql_;
    std::string text_;
    std::vector<bool> binary_;
    std::vector<std::string> args_;
};

// 无参数语句复用相同的结果读取合同。
class Statement final {
public:
    // 借用会话执行无参数的驱动语句。
    explicit Statement(Session db) : db_(db) {
        if (db.Mysql()) mysql_.reset(db.Mysql()->createStatement());
    }

    // 执行读取，返回由调用方持有的结果集。
    ResultSet* ExecuteQuery(const QueryText& text) {
        return mysql_ ? new ResultSet(mysql_->executeQuery(text.mysql))
                      : new ResultSet(db_.Postgres()->Query(text.postgres.c_str()));
    }

    // 执行写语句并返回受影响的记录数。
    int ExecuteUpdate(const QueryText& text) {
        if (mysql_) return mysql_->executeUpdate(text.mysql);
        PreparedStatement prepared(db_, text);
        return prepared.ExecuteUpdate();
    }

    // 执行无需消费结果集的语句。
    bool Execute(const QueryText& text) {
        if (mysql_) return mysql_->execute(text.mysql);
        ExecuteUpdate(text);
        return false;
    }

private:
    Session db_;
    std::unique_ptr<sql::Statement> mysql_;
};

// 为当前物理连接选择方言并创建待绑定的语句。
inline PreparedStatement* Session::PrepareStatement(const QueryText& text) const {
    return new PreparedStatement(*this, text);
}

// 创建无参数语句，由调用方管理其生命周期。
inline Statement* Session::CreateStatement() const {
    return new Statement(*this);
}

// 两个驱动独立报告唯一键冲突，其他故障交给上层原有的存储错误处理。
inline bool Duplicate(const std::exception& error) {
    if (const auto* pg = dynamic_cast<const chat_pg::Error*>(&error)) return pg->state == "23505";
    if (const auto* my = dynamic_cast<const sql::SQLException*>(&error)) return my->getErrorCode() == 1062;
    return false;
}
} // namespace database
