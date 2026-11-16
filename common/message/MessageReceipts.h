#pragma once

#include "MessagePersistence.h"
#include <algorithm>
#include <chrono>
#include <set>

namespace messaging {
/** @brief 携带可映射为协议错误的回执校验失败，不承载底层凭据。 */
class ReceiptError final : public std::runtime_error {
public:
    /** @brief 初始化ReceiptError，携带可映射为协议错误的回执校验失败，不承载底层凭据。 */
    explicit ReceiptError(const char* code) : std::runtime_error(code) {}
};

/** @brief 解析回执 revision，拒绝非规范或越界数值。 */
inline std::int64_t ReceiptRevision(const Json::Value& value) {
    if (!value.isString())
        throw ReceiptError("InvalidRequest");
    const auto text = value.asString();
    if (text.empty() || text.size() > 19 || (text.size() > 1 && text[0] == '0'))
        throw ReceiptError("InvalidRequest");
    std::int64_t result = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9' || result > (INT64_MAX - (digit - '0')) / 10)
            throw ReceiptError("InvalidRequest");
        result = result * 10 + digit - '0';
    }
    return result;
}

/** @brief 验证回执请求对象及协议字段，非法输入抛出带业务码的错误。 */
inline void ValidateReceiptRequest(const Json::Value& request, bool report) {
    if (!request.isObject() || !request["version"].isInt() || request["version"].asInt() != 1 ||
        !request["chat_id"].isInt() || request["chat_id"].asInt() <= 0 || !request["request_id"].isString() ||
        request["request_id"].asString().empty() || request["request_id"].asString().size() > 64 ||
        CompactJson(request).size() > 2048)
        throw ReceiptError("InvalidRequest");
    if (!report) {
        ReceiptRevision(request["after_revision"]);
        return;
    }
    const auto& items = request["items"];
    if (!items.isArray() || items.empty() || items.size() > 8)
        throw ReceiptError("InvalidRequest");
    std::set<int> ids;
    for (const auto& item : items) {
        if (!item.isObject() || !item["message_id"].isInt() || item["message_id"].asInt() <= 0 ||
            !ids.insert(item["message_id"].asInt()).second || !item["level"].isString() ||
            (item["level"].asString() != "delivered" && item["level"].asString() != "read"))
            throw ReceiptError("InvalidRequest");
    }
}

/** @brief 将当前数据库行序列化为协议回执；要求调用方已定位有效行，数据库读取异常向上传播。 */
inline Json::Value ReadReceipt(database::ResultSet& row) {
    Json::Value item;
    item["message_id"] = row.GetInt("message_id");
    item["recipient_uid"] = row.GetInt("recipient_uid");
    item["level"] = row.GetInt("level") == 2 ? "read" : "delivered";
    item["revision"] = std::to_string(row.GetInt64("revision"));
    item["delivered_at"] = Json::Int64(row.GetInt64("delivered_at"));
    item["read_at"] = row.IsNull("read_at") ? Json::Value() : Json::Value(Json::Int64(row.GetInt64("read_at")));
    return item;
}

// The conversation lock serializes both version allocation and page reads with commit.
/**
 * @brief 在事务内锁定会话并核对成员；report 为真时单调更新回执及版本，否则按版本分页读取。
 * response 填入回执项与游标，peer 写入另一成员编号；仅正常返回时可使用这些输出。
 * 非法输入、权限或期限失败抛 ReceiptError，数据库异常向上传播，未提交事务由守卫回滚。
 * 五秒期限在事务阶段间检查，不能取消已开始的同步数据库调用。
 */
inline void ReceiptRequest(
    database::Session connection,
    int uid,
    const Json::Value& request,
    bool report,
    Json::Value& response,
    int& peer) {
    ValidateReceiptRequest(request, report);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    // 在事务关键阶段检查截止时间，超时抛回执期限错误。
    const auto check_deadline = [&] {
        if (std::chrono::steady_clock::now() >= deadline)
            throw ReceiptError("DeadlineExceeded");
    };
    const int chat = request["chat_id"].asInt();
    Transaction transaction(connection);
    std::unique_ptr<database::PreparedStatement> membership(
        connection.PrepareStatement("SELECT user1_id,user2_id FROM private_chat WHERE chat_id=? FOR UPDATE"));
    membership->SetInt(1, chat);
    std::unique_ptr<database::ResultSet> members(membership->ExecuteQuery());
    if (!members->Next() || (members->GetInt(1) != uid && members->GetInt(2) != uid))
        throw ReceiptError("Unauthorized");
    peer = members->GetInt(1) == uid ? members->GetInt(2) : members->GetInt(1);
    members.reset();
    response["items"] = Json::Value(Json::arrayValue);
    if (report) {
        std::vector<std::pair<int, int>> items;
        for (const auto& item : request["items"])
            items.emplace_back(item["message_id"].asInt(), item["level"].asString() == "read" ? 2 : 1);
        std::sort(items.begin(), items.end());
        for (const auto& item : items) {
            check_deadline();
            std::unique_ptr<database::PreparedStatement> check(
                connection.PrepareStatement("SELECT recv_id FROM chat_message WHERE chat_id=? AND message_id=?"));
            check->SetInt(1, chat);
            check->SetInt(2, item.first);
            std::unique_ptr<database::ResultSet> row(check->ExecuteQuery());
            if (!row->Next() || row->GetInt(1) != uid)
                throw ReceiptError("InvalidMessage");
        }
        std::unique_ptr<database::PreparedStatement> create_clock(connection.PrepareStatement(database::QueryText{
            "INSERT IGNORE INTO private_chat_receipt_clock(chat_id,last_revision) VALUES(?,0)",
            "INSERT INTO private_chat_receipt_clock(chat_id,last_revision) VALUES(?,0) ON CONFLICT DO NOTHING"}));
        create_clock->SetInt(1, chat);
        create_clock->ExecuteUpdate();
        std::unique_ptr<database::PreparedStatement> clock(
            connection.PrepareStatement("SELECT last_revision FROM private_chat_receipt_clock WHERE chat_id=?"));
        clock->SetInt(1, chat);
        std::unique_ptr<database::ResultSet> version(clock->ExecuteQuery());
        if (!version->Next())
            throw ReceiptError("StorageUnavailable");
        auto revision = version->GetInt64(1);
        version.reset();
        const auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        for (const auto& item : items) {
            check_deadline();
            std::unique_ptr<database::PreparedStatement> select(connection.PrepareStatement(
                "SELECT * FROM chat_message_receipt WHERE chat_id=? AND message_id=? AND recipient_uid=?"));
            select->SetInt(1, chat);
            select->SetInt(2, item.first);
            select->SetInt(3, uid);
            std::unique_ptr<database::ResultSet> prior(select->ExecuteQuery());
            Json::Value saved;
            const bool exists = prior->Next();
            if (exists)
                saved = ReadReceipt(*prior);
            const int level = exists ? prior->GetInt("level") : 0;
            prior.reset();
            if (item.second > level) {
                if (revision == INT64_MAX)
                    throw ReceiptError("StorageUnavailable");
                ++revision;
                std::unique_ptr<database::PreparedStatement> write(connection.PrepareStatement(database::QueryText{
                    "INSERT INTO chat_message_receipt(chat_id,message_id,recipient_uid,level,delivered_at,read_at,revision) "
                    "VALUES(?,?,?,?,?,NULLIF(?,0),?) ON DUPLICATE KEY UPDATE "
                    "level=VALUES(level),read_at=VALUES(read_at),revision=VALUES(revision)",
                    "INSERT INTO "
                    "chat_message_receipt(chat_id,message_id,recipient_uid,level,delivered_at,read_at,revision) "
                    "VALUES(?,?,?,?,?,NULLIF(?::bigint,0),?) ON CONFLICT (chat_id,message_id,recipient_uid) DO "
                    "UPDATE SET level=EXCLUDED.level,read_at=EXCLUDED.read_at,revision=EXCLUDED.revision"}));
                write->SetInt(1, chat);
                write->SetInt(2, item.first);
                write->SetInt(3, uid);
                write->SetInt(4, item.second);
                write->SetInt64(5, now);
                write->SetInt64(6, item.second == 2 ? now : 0);
                write->SetInt64(7, revision);
                write->ExecuteUpdate();
                std::unique_ptr<database::ResultSet> updated(select->ExecuteQuery());
                if (!updated->Next())
                    throw ReceiptError("StorageUnavailable");
                saved = ReadReceipt(*updated);
            }
            response["items"].append(saved);
        }
        std::unique_ptr<database::PreparedStatement> advance(
            connection.PrepareStatement("UPDATE private_chat_receipt_clock SET last_revision=? WHERE chat_id=?"));
        advance->SetInt64(1, revision);
        advance->SetInt(2, chat);
        advance->ExecuteUpdate();
        response["latest_revision"] = std::to_string(revision);
        if (CompactJson(response).size() > 2048)
            throw ReceiptError("InvalidRequest");
    } else {
        const auto after = ReceiptRevision(request["after_revision"]);
        response["after_revision"] = std::to_string(after);
        response["next_revision"] = std::to_string(after);
        response["load_more"] = false;
        std::unique_ptr<database::PreparedStatement> select(connection.PrepareStatement(
            "SELECT * FROM chat_message_receipt WHERE chat_id=? AND revision>? ORDER BY revision LIMIT 17"));
        select->SetInt(1, chat);
        select->SetInt64(2, after);
        std::unique_ptr<database::ResultSet> rows(select->ExecuteQuery());
        while (rows->Next()) {
            Json::Value candidate = response;
            auto item = ReadReceipt(*rows);
            candidate["items"].append(item);
            candidate["next_revision"] = item["revision"];
            if (candidate["items"].size() > 16 || CompactJson(candidate).size() > 2048) {
                if (response["items"].empty())
                    throw ReceiptError("InvalidRequest");
                response["load_more"] = true;
                break;
            }
            response = std::move(candidate);
        }
    }
    check_deadline();
    transaction.Commit();
}
}
