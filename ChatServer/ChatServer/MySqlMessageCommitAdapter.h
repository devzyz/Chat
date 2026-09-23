#pragma once

#include "MessageCommit.h"

#include <memory>

namespace sql { class Connection; }

namespace message_commit {

/** @brief 隔离最终 SQL COMMIT 操作的同步接口，供生产连接或故障注入使用。 */
class TransactionCommit {
public:
    /** @brief 允许经接口销毁具体提交操作。 */
    virtual ~TransactionCommit() = default;
    /** @brief 提交借用连接上的当前事务；失败可抛异常，由适配器映射提交结果。 */
    virtual void Commit(sql::Connection& connection) = 0;
};

/** @brief 独占借用 MySQL 连接提交批次；连接及可选提交器必须比适配器存活更久，不得并发使用。 */
class MySqlMessageCommitAdapter final : public Store {
public:
    /** @brief 借用连接并使用其原生 commit；不接管连接所有权。 */
    explicit MySqlMessageCommitAdapter(sql::Connection& connection) : _connection(connection) {}
    /** @brief 同时借用连接和自定义最终提交器，用于控制提交失败边界。 */
    MySqlMessageCommitAdapter(sql::Connection& connection, TransactionCommit& commit)
        : _connection(connection), _commit(&commit) {}
    /**
     * @brief 锁定私聊行，在一个事务内提交批次并校验重复 UUID 的内容及归属；失败回滚。
     * @note 要求连接处于自动提交；期限在 SQL 操作间检查，不取消在途 SQL。已确认 COMMIT
     * 优先于随后超时或清理失败；错误结果也可能对应提交结果未知，重试需保留 UUID。
     */
    Result Commit(int sender, int recipient, int chat, const Batch& batch, Deadline deadline) override;
    /** @brief 查询连接是否仍可归还池；false 时调用方必须丢弃连接，不能重新借出。 */
    bool IsReusable() const { return _is_reusable; }
private:
    sql::Connection& _connection;
    TransactionCommit* _commit = nullptr;
    bool _is_reusable = true;
};

/**
 * @brief 建立独占连接并选择 schema，连接/读/写及 InnoDB 锁等待分别设置为 2 秒。
 * @note 仅接受 localhost 或数字端点，关闭自动重连；不是整个调用的硬期限，失败抛异常。
 */
std::unique_ptr<sql::Connection> ConnectBounded(const std::string& url, const std::string& user,
    const std::string& password, const std::string& schema);

}
