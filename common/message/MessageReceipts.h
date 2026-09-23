#pragma once

#include "MessagePersistence.h"
#include <algorithm>
#include <chrono>
#include <set>

namespace messaging {
class ReceiptError final : public std::runtime_error {
public:
    explicit ReceiptError(const char* code) : std::runtime_error(code) {}
};

inline std::int64_t ReceiptRevision(const Json::Value& value) {
    if (!value.isString()) throw ReceiptError("InvalidRequest");
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

inline void ValidateReceiptRequest(const Json::Value& request, bool report) {
    if (!request.isObject() || !request["version"].isInt() || request["version"].asInt() != 1
        || !request["chat_id"].isInt() || request["chat_id"].asInt() <= 0
        || !request["request_id"].isString() || request["request_id"].asString().empty()
        || request["request_id"].asString().size() > 64 || CompactJson(request).size() > 2048)
        throw ReceiptError("InvalidRequest");
    if (!report) { ReceiptRevision(request["after_revision"]); return; }
    const auto& items = request["items"];
    if (!items.isArray() || items.empty() || items.size() > 8) throw ReceiptError("InvalidRequest");
    std::set<int> ids;
    for (const auto& item : items) {
        if (!item.isObject() || !item["message_id"].isInt() || item["message_id"].asInt() <= 0
            || !ids.insert(item["message_id"].asInt()).second || !item["level"].isString()
            || (item["level"].asString() != "delivered" && item["level"].asString() != "read"))
            throw ReceiptError("InvalidRequest");
    }
}

inline Json::Value ReadReceipt(sql::ResultSet& row) {
    Json::Value item;
    item["message_id"] = row.getInt("message_id");
    item["recipient_uid"] = row.getInt("recipient_uid");
    item["level"] = row.getInt("level") == 2 ? "read" : "delivered";
    item["revision"] = std::to_string(row.getInt64("revision"));
    item["delivered_at"] = Json::Int64(row.getInt64("delivered_at"));
    item["read_at"] = row.isNull("read_at") ? Json::Value() : Json::Value(Json::Int64(row.getInt64("read_at")));
    return item;
}

// The conversation lock serializes both version allocation and page reads with commit.
inline void ReceiptRequest(sql::Connection& connection, int uid, const Json::Value& request,
                           bool report, Json::Value& response, int& peer) {
    ValidateReceiptRequest(request, report);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto check_deadline = [&] {
        if (std::chrono::steady_clock::now() >= deadline) throw ReceiptError("DeadlineExceeded");
    };
    const int chat = request["chat_id"].asInt();
    Transaction transaction(connection);
    std::unique_ptr<sql::PreparedStatement> membership(connection.prepareStatement(
        "SELECT user1_id,user2_id FROM private_chat WHERE chat_id=? FOR UPDATE"));
    membership->setInt(1, chat);
    std::unique_ptr<sql::ResultSet> members(membership->executeQuery());
    if (!members->next() || (members->getInt(1) != uid && members->getInt(2) != uid))
        throw ReceiptError("Unauthorized");
    peer = members->getInt(1) == uid ? members->getInt(2) : members->getInt(1);
    members.reset();
    response["items"] = Json::Value(Json::arrayValue);
    if (report) {
        std::vector<std::pair<int, int>> items;
        for (const auto& item : request["items"])
            items.emplace_back(item["message_id"].asInt(), item["level"].asString() == "read" ? 2 : 1);
        std::sort(items.begin(), items.end());
        for (const auto& item : items) {
            check_deadline();
            std::unique_ptr<sql::PreparedStatement> check(connection.prepareStatement(
                "SELECT recv_id FROM chat_message WHERE chat_id=? AND message_id=?"));
            check->setInt(1, chat); check->setInt(2, item.first);
            std::unique_ptr<sql::ResultSet> row(check->executeQuery());
            if (!row->next() || row->getInt(1) != uid) throw ReceiptError("InvalidMessage");
        }
        std::unique_ptr<sql::PreparedStatement> create_clock(connection.prepareStatement(
            "INSERT IGNORE INTO private_chat_receipt_clock(chat_id,last_revision) VALUES(?,0)"));
        create_clock->setInt(1, chat); create_clock->executeUpdate();
        std::unique_ptr<sql::PreparedStatement> clock(connection.prepareStatement(
            "SELECT last_revision FROM private_chat_receipt_clock WHERE chat_id=?"));
        clock->setInt(1, chat);
        std::unique_ptr<sql::ResultSet> version(clock->executeQuery());
        if (!version->next()) throw ReceiptError("StorageUnavailable");
        auto revision = version->getInt64(1);
        version.reset();
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        for (const auto& item : items) {
            check_deadline();
            std::unique_ptr<sql::PreparedStatement> select(connection.prepareStatement(
                "SELECT * FROM chat_message_receipt WHERE chat_id=? AND message_id=? AND recipient_uid=?"));
            select->setInt(1, chat); select->setInt(2, item.first); select->setInt(3, uid);
            std::unique_ptr<sql::ResultSet> prior(select->executeQuery());
            Json::Value saved;
            const bool exists = prior->next();
            if (exists) saved = ReadReceipt(*prior);
            const int level = exists ? prior->getInt("level") : 0;
            prior.reset();
            if (item.second > level) {
                if (revision == INT64_MAX) throw ReceiptError("StorageUnavailable");
                ++revision;
                std::unique_ptr<sql::PreparedStatement> write(connection.prepareStatement(
                    "INSERT INTO chat_message_receipt(chat_id,message_id,recipient_uid,level,delivered_at,read_at,revision) "
                    "VALUES(?,?,?,?,?,NULLIF(?,0),?) ON DUPLICATE KEY UPDATE "
                    "level=VALUES(level),read_at=VALUES(read_at),revision=VALUES(revision)"));
                write->setInt(1, chat); write->setInt(2, item.first); write->setInt(3, uid);
                write->setInt(4, item.second); write->setInt64(5, now);
                write->setInt64(6, item.second == 2 ? now : 0); write->setInt64(7, revision);
                write->executeUpdate();
                std::unique_ptr<sql::ResultSet> updated(select->executeQuery());
                if (!updated->next()) throw ReceiptError("StorageUnavailable");
                saved = ReadReceipt(*updated);
            }
            response["items"].append(saved);
        }
        std::unique_ptr<sql::PreparedStatement> advance(connection.prepareStatement(
            "UPDATE private_chat_receipt_clock SET last_revision=? WHERE chat_id=?"));
        advance->setInt64(1, revision); advance->setInt(2, chat); advance->executeUpdate();
        response["latest_revision"] = std::to_string(revision);
        if (CompactJson(response).size() > 2048) throw ReceiptError("InvalidRequest");
    } else {
        const auto after = ReceiptRevision(request["after_revision"]);
        response["after_revision"] = std::to_string(after);
        response["next_revision"] = std::to_string(after);
        response["load_more"] = false;
        std::unique_ptr<sql::PreparedStatement> select(connection.prepareStatement(
            "SELECT * FROM chat_message_receipt WHERE chat_id=? AND revision>? ORDER BY revision LIMIT 17"));
        select->setInt(1, chat); select->setInt64(2, after);
        std::unique_ptr<sql::ResultSet> rows(select->executeQuery());
        while (rows->next()) {
            Json::Value candidate = response;
            auto item = ReadReceipt(*rows);
            candidate["items"].append(item);
            candidate["next_revision"] = item["revision"];
            if (candidate["items"].size() > 16 || CompactJson(candidate).size() > 2048) {
                if (response["items"].empty()) throw ReceiptError("InvalidRequest");
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
