#pragma once

#include "MessageCommit.h"

#include <memory>

namespace sql { class Connection; }

namespace message_commit {

class TransactionCommit {
public:
    virtual ~TransactionCommit() = default;
    virtual void Commit(sql::Connection& connection) = 0;
};

// Exclusive borrowed connection; unusable sessions must not return to their pool.
class MySqlMessageCommitAdapter final : public Store {
public:
    explicit MySqlMessageCommitAdapter(sql::Connection& connection) : _connection(connection) {}
    MySqlMessageCommitAdapter(sql::Connection& connection, TransactionCommit& commit)
        : _connection(connection), _commit(&commit) {}
    Result Commit(int sender, int recipient, int chat, const Batch& batch, Deadline deadline) override;
    bool IsReusable() const { return _is_reusable; }
private:
    sql::Connection& _connection;
    TransactionCommit* _commit = nullptr;
    bool _is_reusable = true;
};

std::unique_ptr<sql::Connection> ConnectBounded(const std::string& url, const std::string& user,
    const std::string& password, const std::string& schema);

}
