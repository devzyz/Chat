#pragma once
#include "MessageCommit.h"
#include "../../common/postgres/Connection.h"
#include <json/json.h>
#include <functional>

namespace message_commit {
// 每个适配器借用一个物理连接，在同一事务校验会话、关系版本和消息身份。
class PostgresMessageCommitAdapter final : public Store {
public:
    using CommitAction = std::function<void(chat_pg::Connection&, Deadline)>;
    // 可选提交动作供集成测试在真实提交前后注入连接故障。
    PostgresMessageCommitAdapter(chat_pg::Connection& db, CommitAction action = {})
        : db_(db), action_(std::move(action)) {}
    // 提交当前事务并保留驱动的错误分类。
    Result Commit(int sender, int recipient, int chat, const Batch& batch, Deadline deadline, Preconditions access = {})
        override {
        if (!db_.GetAutoCommit()) {
            db_.Close();
            return {Error::STORAGE_UNAVAILABLE, {}};
        }
        bool committing = false;
        try {
            Json::Value inputs(Json::arrayValue);
            for (const auto& message : batch) {
                Json::Value item;
                item["uuid"] = message.first;
                item["body"] = chat_pg::Hex(message.second);
                inputs.append(std::move(item));
            }
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            db_.Query("BEGIN ISOLATION LEVEL READ COMMITTED", {}, deadline);
            auto rows = db_.Query(
                "SELECT * FROM chat.commit_text($1,$2,$3,$4,$5,$6)",
                {std::to_string(sender),
                 std::to_string(recipient),
                 std::to_string(chat),
                 std::to_string(access.relationship_revision),
                 std::to_string(access.group_epoch),
                 Json::writeString(writer, inputs)},
                deadline);
            Result result;
            for (int i = 0; i < PQntuples(rows.get()); ++i) {
                result.items.push_back(
                    {std::stoi(PQgetvalue(rows.get(), i, 0)),
                     PQgetvalue(rows.get(), i, 1),
                     std::strcmp(PQgetvalue(rows.get(), i, 2), "t") == 0 ? Disposition::CREATED : Disposition::EXISTING,
                     std::stoll(PQgetvalue(rows.get(), i, 3))});
            }
            if (result.items.size() != batch.size())
                throw chat_pg::Error("P1003");
            committing = true;
            if (action_)
                action_(db_, deadline);
            else
                db_.Query("COMMIT", {}, deadline);
            return result;
        } catch (const chat_pg::Error& error) {
            if (committing)
                db_.Close();
            Rollback();
            if (error.state == "P1001")
                return {Error::INVALID_MEMBERSHIP, {}};
            if (error.state == "P1002")
                return {Error::CONFLICT, {}};
            if (error.state == "55P03" || error.state == "57014" || std::chrono::steady_clock::now() >= deadline)
                return {Error::DEADLINE_EXCEEDED, {}};
            return {Error::STORAGE_UNAVAILABLE, {}};
        } catch (const std::exception&) {
            if (committing)
                db_.Close();
            Rollback();
            return {Error::STORAGE_UNAVAILABLE, {}};
        }
    }

private:
    // 仅在仍可通信时回滚；协议或清理失败后丢弃物理连接。
    void Rollback() noexcept {
        try {
            if (!db_.IsClosed() && !db_.GetAutoCommit())
                db_.Rollback();
        } catch (...) {
            db_.Close();
        }
    }
    chat_pg::Connection& db_;
    CommitAction action_;
};
} // namespace message_commit
