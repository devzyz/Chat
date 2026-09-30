#pragma once

#include "MessagePersistence.h"
#include <set>

namespace messaging {
/** @brief 群业务可预期拒绝；协议边界只发布此安全错误码。 */
class GroupError : public std::runtime_error {
public:
    /** @brief 保存稳定的业务错误码。 */
    explicit GroupError(const char* code) : std::runtime_error(code) {}
};

/** @brief 持有预编译语句和结果集，避免结果集活得比语句更久；参数仅绑定不拼接。 */
class GroupQuery final {
public:
    /** @brief 绑定内部生成的参数并执行读取或更新，数据库异常交给调用事务处理。 */
    GroupQuery(sql::Connection& db, const std::string& sql, const std::vector<std::string>& values, bool read = true)
        : _statement(db.prepareStatement(read && sql.find("FOR UPDATE") == std::string::npos ? sql + " FOR UPDATE" : sql)) {
        for (std::size_t index = 0; index < values.size(); ++index) _statement->setString(static_cast<int>(index + 1), values[index]);
        if (read) rows.reset(_statement->executeQuery());
        else _statement->executeUpdate();
    }
private:
    std::unique_ptr<sql::PreparedStatement> _statement;
public:
    std::unique_ptr<sql::ResultSet> rows;
};

/** @brief 读取用户曾加入群的目录快照；退出状态保留，缺失不被解释为删除。 */
inline Json::Value GroupDirectory(sql::Connection& db, int chat, int uid) {
    GroupQuery query(db, "SELECT g.name,g.owner_uid,g.revision,g.dissolved,m.state,m.membership_epoch,m.joined_after_id "
        "FROM group_chat g JOIN group_chat_member m ON m.chat_id=g.chat_id WHERE g.chat_id=? AND m.user_id=?",
        {std::to_string(chat), std::to_string(uid)});
    if (!query.rows->next()) throw GroupError("NotMember");
    Json::Value result;
    result["chat_id"] = chat; result["type"] = "group";
    result["group_name"] = query.rows->getString(1).asStdString();
    result["owner_uid"] = query.rows->getInt(2);
    result["group_revision"] = std::to_string(query.rows->getInt64(3));
    result["group_state"] = query.rows->getBoolean(4) ? "dissolved" : query.rows->getString(5).asStdString();
    result["membership_epoch"] = std::to_string(query.rows->getInt64(6));
    result["joined_after_id"] = Json::Int64(query.rows->getInt64(7));
    return result;
}

/** @brief 检查规范正十进制版本，避免 JSON 精度或溢出改变并发比较。 */
inline std::int64_t GroupNumber(const Json::Value& value) {
    if (!value.isString()) throw GroupError("InvalidRequest");
    try {
        const auto text = value.asString();
        const auto number = std::stoll(text);
        if (number <= 0 || std::to_string(number) != text) throw GroupError("InvalidRequest");
        return number;
    } catch (const std::exception&) { throw GroupError("InvalidRequest"); }
}

/** @brief 群管理在操作者及群锁下事务执行；原请求的结果先于当前权限检查，实现安全重放。 */
inline void ManageGroup(sql::Connection& db, int actor, const Json::Value& request, Json::Value& response) {
    if (!request["chat_id"].isInt() || request["chat_id"].asInt() <= 0 || !request["operation"].isString()
        || !request["request_id"].isString() || request["request_id"].asString().size() != 36)
        throw GroupError("InvalidRequest");
    const auto chat = request["chat_id"].asInt();
    const auto key = request["request_id"].asString();
    const auto actor_text = std::to_string(actor), chat_text = std::to_string(chat);
    const auto payload = CompactJson(request);
    Transaction transaction(db);
    GroupQuery actor_lock(db, "SELECT uid FROM user WHERE uid=? FOR UPDATE", {actor_text});
    if (!actor_lock.rows->next()) throw GroupError("Forbidden");
    GroupQuery prior(db, "SELECT payload,result FROM group_operation WHERE actor_uid=? AND request_id=?", {actor_text, key});
    if (prior.rows->next()) {
        if (prior.rows->getString(1).asStdString() != payload) throw GroupError("RequestConflict");
        Json::Reader reader;
        if (!reader.parse(prior.rows->getString(2).asStdString(), response)) throw GroupError("StorageUnavailable");
        transaction.Commit(); return;
    }
    GroupQuery group(db, "SELECT owner_uid,revision,dissolved FROM group_chat WHERE chat_id=? FOR UPDATE", {chat_text});
    if (!group.rows->next()) throw GroupError("NotMember");
    const int owner = group.rows->getInt(1);
    const auto revision = group.rows->getInt64(2);
    if (group.rows->getBoolean(3)) throw GroupError("GroupDissolved");
    const auto directory = GroupDirectory(db, chat, actor);
    if (directory["group_state"].asString() != "active") throw GroupError("NotMember");
    if (GroupNumber(request["expected_revision"]) != revision) throw GroupError("VersionConflict");
    const auto operation = request["operation"].asString();
    if (operation != "leave" && owner != actor) throw GroupError("Forbidden");
    if (operation == "add") {
        if (!request["members"].isArray() || request["members"].empty() || request["members"].size() > 19)
            throw GroupError("InvalidRequest");
        GroupQuery count(db, "SELECT COUNT(*) FROM group_chat_member WHERE chat_id=? AND state='active'", {chat_text});
        count.rows->next();
        if (count.rows->getInt(1) + request["members"].size() > 20) throw GroupError("MemberLimit");
        std::set<int> members;
        for (const auto& value : request["members"]) {
            if (!value.isInt() || value.asInt() <= 0 || value.asInt() == actor || !members.insert(value.asInt()).second)
                throw GroupError("InvalidRequest");
            const auto target = std::to_string(value.asInt());
            GroupQuery friend_row(db, "SELECT 1 FROM friend f JOIN user u ON u.uid=f.other_id WHERE f.self_id=? AND f.other_id=?", {actor_text,target});
            if (!friend_row.rows->next()) throw GroupError("NotFriend");
            GroupQuery existing(db, "SELECT state FROM group_chat_member WHERE chat_id=? AND user_id=?", {chat_text,target});
            if (existing.rows->next() && existing.rows->getString(1) == "active") throw GroupError("MembershipChanged");
        }
        GroupQuery boundary(db, "SELECT COALESCE(MAX(message_id),0) FROM chat_message WHERE chat_id=?", {chat_text});
        boundary.rows->next();
        for (const int member : members) {
            GroupQuery insert(db, "INSERT INTO group_chat_member(chat_id,user_id,role,state,membership_epoch,joined_after_id) "
                "VALUES(?,?,0,'active',1,?) ON DUPLICATE KEY UPDATE state='active',role=0,"
                "membership_epoch=membership_epoch+1,joined_after_id=VALUES(joined_after_id)",
                {chat_text,std::to_string(member),std::to_string(boundary.rows->getInt64(1))}, false);
        }
    } else if (operation == "remove" || operation == "transfer" || operation == "leave") {
        if (operation != "leave" && (!request["target_uid"].isInt() || request["target_uid"].asInt() <= 0))
            throw GroupError("InvalidRequest");
        const int target = operation == "leave" ? actor : request["target_uid"].asInt();
        if (target == owner) throw GroupError("OwnerMustTransfer");
        GroupQuery member(db, "SELECT 1 FROM group_chat_member WHERE chat_id=? AND user_id=? AND state='active'", {chat_text,std::to_string(target)});
        if (!member.rows->next()) throw GroupError("MembershipChanged");
        if (operation == "transfer") {
            GroupQuery change(db, "UPDATE group_chat SET owner_uid=? WHERE chat_id=?", {std::to_string(target),chat_text}, false);
            GroupQuery roles(db, "UPDATE group_chat_member SET role=(user_id=?) WHERE chat_id=?", {std::to_string(target),chat_text}, false);
        } else {
            GroupQuery leave(db, "UPDATE group_chat_member SET state=? WHERE chat_id=? AND user_id=?",
                {operation == "leave" ? "left" : "removed",chat_text,std::to_string(target)}, false);
        }
    } else if (operation == "rename") {
        if (!request["name"].isString() || request["name"].asString().empty() || request["name"].asString().size() > 60
            || request["name"].asString().find_first_not_of(" \t\r\n") == std::string::npos) throw GroupError("InvalidRequest");
        GroupQuery rename(db, "UPDATE group_chat SET name=? WHERE chat_id=?", {request["name"].asString(),chat_text}, false);
    } else if (operation == "dissolve") {
        GroupQuery dissolve(db, "UPDATE group_chat SET dissolved=1 WHERE chat_id=?", {chat_text}, false);
    } else throw GroupError("InvalidRequest");
    GroupQuery version(db, "UPDATE group_chat SET revision=revision+1 WHERE chat_id=?", {chat_text}, false);
    response = GroupDirectory(db, chat, actor);
    response["request_id"] = key; response["error"] = 0;
    GroupQuery record(db, "INSERT INTO group_operation(actor_uid,request_id,chat_id,payload,result) VALUES(?,?,?,?,?)",
        {actor_text,key,chat_text,payload,CompactJson(response)}, false);
    transaction.Commit();
}

/** @brief 在一致群快照中按 UID 分页读取成员，完整响应遵守普通 TCP 帧上限。 */
inline void ReadGroup(sql::Connection& db, int uid, const Json::Value& request, Json::Value& response) {
    if (!request["chat_id"].isInt() || request["chat_id"].asInt() <= 0 || !request["after_uid"].isInt()
        || request["after_uid"].asInt() < 0) throw GroupError("InvalidRequest");
    const int chat = request["chat_id"].asInt(), after = request["after_uid"].asInt();
    Transaction transaction(db);
    try { LockGroup(db, chat, uid); }
    catch (const std::runtime_error&) { throw GroupError("NotMember"); }
    response = GroupDirectory(db, chat, uid);
    response["error"] = 0; response["request_id"] = request["request_id"];
    response["members"] = Json::Value(Json::arrayValue); response["load_more"] = false; response["next_uid"] = after;
    GroupQuery count(db, "SELECT COUNT(*) FROM group_chat_member WHERE chat_id=? AND state='active'", {std::to_string(chat)});
    count.rows->next(); response["member_count"] = count.rows->getInt(1);
    GroupQuery members(db, "SELECT m.user_id,m.role,u.name FROM group_chat_member m JOIN user u ON u.uid=m.user_id "
        "WHERE m.chat_id=? AND m.state='active' AND m.user_id>? ORDER BY m.user_id", {std::to_string(chat),std::to_string(after)});
    while (members.rows->next()) {
        Json::Value item;
        item["uid"] = members.rows->getInt(1); item["role"] = members.rows->getInt(2);
        item["name"] = members.rows->getString(3).asStdString();
        auto candidate = response; candidate["members"].append(item); candidate["next_uid"] = item["uid"];
        if (CompactJson(candidate).size() > 2000) {
            if (response["members"].empty()) throw GroupError("StorageUnavailable");
            response["load_more"] = true; break;
        }
        response = std::move(candidate);
    }
    transaction.Commit();
}
}
