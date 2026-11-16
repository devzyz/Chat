#pragma once
#include "../message/GroupMembership.h"

namespace social {
/** @brief 社交查询拥有语句与结果，是否加锁由调用 SQL 明确决定。 */
class Query final {
public:
    /** @brief 绑定参数并执行有限查询；写语句不创建结果集。 */
    Query(
        database::Session db,
        const database::QueryText& text,
        const std::vector<std::string>& args = {},
        bool read = true)
        : statement(db.PrepareStatement(text)) {
        for (std::size_t i = 0; i < args.size(); ++i)
            statement->SetString(static_cast<int>(i + 1), args[i]);
        if (read)
            rows.reset(statement->ExecuteQuery());
        else
            statement->ExecuteUpdate();
    }

private:
    std::unique_ptr<database::PreparedStatement> statement;

public:
    std::unique_ptr<database::ResultSet> rows;
};
/** @brief 读取规范非负版本或游标，拒绝浮点数和精度损失。 */
inline std::int64_t Number(const Json::Value& value) {
    if (value.isString() && value.asString() == "0")
        return 0;
    return messaging::GroupNumber(value);
}
/** @brief 从数据库投影公开资料，密码、邮箱和令牌不进入结果。 */
inline Json::Value Profile(database::Session db, int uid) {
    Query query(
        db,
        database::QueryText{
            "SELECT uid,name,description,icon,sex,profile_revision FROM user WHERE uid=?",
            "SELECT uid,name,description,icon,sex,profile_revision FROM account WHERE uid=?"},
        {std::to_string(uid)});
    if (!query.rows->Next())
        throw messaging::GroupError("UnknownUser");
    Json::Value row;
    row["id"] = uid;
    row["uid"] = uid;
    row["name"] = query.rows->GetString(2);
    row["description"] = query.rows->GetString(3);
    row["icon"] = query.rows->GetString(4);
    row["sex"] = query.rows->GetInt(5);
    row["profile_revision"] = std::to_string(query.rows->GetInt64(6));
    return row;
}
/** @brief 返回双方私聊关系及操作者的备注；不存在时给出版本零。 */
inline Json::Value Relation(database::Session db, int actor, int peer) {
    auto result = Profile(db, peer);
    Query row(
        db,
        "SELECT chat_id,relationship_active,relationship_revision FROM private_chat WHERE user1_id=? AND user2_id=?",
        {std::to_string(std::min(actor, peer)), std::to_string(std::max(actor, peer))});
    result["relationship_active"] = false;
    result["relationship_revision"] = "0";
    if (row.rows->Next()) {
        result["chat_id"] = row.rows->GetInt(1);
        result["relationship_active"] = row.rows->GetBoolean(2);
        result["relationship_revision"] = std::to_string(row.rows->GetInt64(3));
    }
    Query remark(
        db,
        "SELECT backname FROM friend WHERE self_id=? AND other_id=? ORDER BY id LIMIT 1",
        {std::to_string(actor), std::to_string(peer)});
    result["backname"] = remark.rows->Next() ? remark.rows->GetString(1) : "";
    Query outgoing(
        db,
        "SELECT revision,status FROM apply_friend WHERE from_uid=? AND to_uid=?",
        {std::to_string(actor), std::to_string(peer)});
    result["outgoing_revision"] = "0";
    result["outgoing_status"] = -1;
    if (outgoing.rows->Next()) {
        result["outgoing_revision"] = std::to_string(outgoing.rows->GetInt64(1));
        result["outgoing_status"] = outgoing.rows->GetInt(2);
    }
    return result;
}
/** @brief 按稳定用户编号分页，逐条预算完整回包，明确返回失效关系。 */
inline void Read(database::Session db, int actor, const Json::Value& request, Json::Value& response) {
    messaging::Transaction transaction(db);
    const auto kind = request["kind"].asString();
    response["kind"] = kind;
    if (kind == "profile") {
        const int peer = request.get("target_uid", actor).asInt();
        response["profile"] = peer == actor ? Profile(db, actor) : Relation(db, actor, peer);
        transaction.Commit();
        return;
    }
    const auto after = Number(request["after"]);
    response["after"] = std::to_string(after);
    response["next"] = std::to_string(after);
    response["items"] = Json::Value(Json::arrayValue);
    response["load_more"] = false;
    const auto self = std::to_string(actor);
    const auto text =
        kind == "contacts"
            ? database::QueryText{"SELECT IF(user1_id=?,user2_id,user1_id) AS peer FROM private_chat WHERE (user1_id=? OR user2_id=?) "
              "AND IF(user1_id=?,user2_id,user1_id)>? ORDER BY peer LIMIT 51",
                "SELECT (CASE WHEN user1_id=? THEN user2_id ELSE user1_id END) AS peer FROM private_chat WHERE "
                "(user1_id=? OR user2_id=?) AND (CASE WHEN user1_id=? THEN user2_id ELSE user1_id END)>? ORDER BY "
                "peer LIMIT 51"}
            : "SELECT from_uid FROM apply_friend WHERE to_uid=? AND from_uid>? ORDER BY from_uid LIMIT 51";
    if (kind != "contacts" && kind != "applications")
        throw messaging::GroupError("InvalidRequest");
    Query ids(
        db,
        text,
        kind == "contacts" ? std::vector<std::string>{self, self, self, self, std::to_string(after)}
                           : std::vector<std::string>{self, std::to_string(after)});
    while (ids.rows->Next()) {
        const int peer = ids.rows->GetInt(1);
        auto item = kind == "contacts" ? Relation(db, actor, peer) : Profile(db, peer);
        if (kind == "applications") {
            Query application(
                db,
                "SELECT status,revision,description,backname FROM apply_friend WHERE from_uid=? AND to_uid=?",
                {std::to_string(peer), self});
            if (!application.rows->Next())
                continue;
            item["fromuid"] = peer;
            item["touid"] = actor;
            item["status"] = application.rows->GetInt(1);
            item["application_revision"] = std::to_string(application.rows->GetInt64(2));
            item["applydescription"] = item["description"];
            item["description"] = application.rows->GetString(3);
            item["backname"] = application.rows->GetString(4);
        }
        auto candidate = response;
        candidate["items"].append(item);
        candidate["next"] = std::to_string(peer);
        candidate["load_more"] = true;
        if (response["items"].size() >= 50 || messaging::CompactJson(candidate).size() > 8000) {
            if (response["items"].empty())
                throw messaging::GroupError("ResponseTooLarge");
            response["load_more"] = true;
            break;
        }
        response["items"].append(item);
        response["next"] = std::to_string(peer);
    }
    transaction.Commit();
}
/** @brief 在数据库中校验字符数，完整协议帧另由传输层限制。 */
inline void ValidateText(database::Session db, const Json::Value& value, bool nonempty) {
    if (!value.isString())
        throw messaging::GroupError("InvalidRequest");
    Query size(
        db,
        database::QueryText{
            "SELECT CHAR_LENGTH(?),CHAR_LENGTH(TRIM(?))",
            "SELECT chat.utf8_length(?b),chat.utf8_length(chat.trim_ascii_space(?b))"},
        {value.asString(), value.asString()});
    if (!size.rows->Next() || size.rows->GetInt(1) > 255 || (nonempty && size.rows->GetInt(2) == 0))
        throw messaging::GroupError("InvalidRequest");
}
/** @brief 在本人行锁下按版本修改用户名及描述，唯一约束决定重名冲突。 */
inline void UpdateProfile(database::Session db, int actor, const Json::Value& request, Json::Value& response) {
    ValidateText(db, request["name"], true);
    ValidateText(db, request["description"], false);
    messaging::Transaction transaction(db);
    Query current(
        db,
        database::QueryText{
            "SELECT profile_revision FROM user WHERE uid=? FOR UPDATE",
            "SELECT profile_revision FROM account WHERE uid=? FOR UPDATE"},
        {std::to_string(actor)});
    if (!current.rows->Next() || current.rows->GetInt64(1) != Number(request["expected_revision"]))
        throw messaging::GroupError("VersionConflict");
    Query update(
        db,
        database::QueryText{
            "UPDATE user SET name=TRIM(?),description=?,profile_revision=profile_revision+1 WHERE uid=?",
            "UPDATE account SET "
            "name=chat.trim_ascii_space(?b),description=?b,profile_revision=profile_revision+1 WHERE uid=?"},
        {request["name"].asString(), request["description"].asString(), std::to_string(actor)},
        false);
    response["profile"]["profile_revision"] = std::to_string(Number(request["expected_revision"]) + 1);
    transaction.Commit();
}
/** @brief 在固定用户锁顺序及会话事务内管理关系，迟到命令不能改变新版本申请或关系。 */
inline void Manage(database::Session db, int actor, const Json::Value& request, Json::Value& response) {
    if (!request["target_uid"].isInt() || request["target_uid"].asInt() <= 0 || request["target_uid"].asInt() == actor)
        throw messaging::GroupError("InvalidRequest");
    const int peer = request["target_uid"].asInt();
    const auto self = std::to_string(actor), other = std::to_string(peer);
    const auto first = std::to_string(std::min(actor, peer)), second = std::to_string(std::max(actor, peer));
    const auto operation = request["operation"].asString();
    const auto expected = Number(request["expected_revision"]);
    messaging::Transaction transaction(db);
    Query users(
        db,
        database::QueryText{
            "SELECT uid FROM user WHERE uid IN (?,?) ORDER BY uid FOR UPDATE",
            "SELECT uid FROM account WHERE uid IN (?,?) ORDER BY uid FOR UPDATE"},
        {first, second});
    int count = 0;
    while (users.rows->Next())
        ++count;
    if (count != 2)
        throw messaging::GroupError("UnknownUser");
    Query relation(
        db,
        "SELECT chat_id,relationship_active,relationship_revision FROM private_chat "
        "WHERE user1_id=? AND user2_id=? FOR UPDATE",
        {first, second});
    const bool found = relation.rows->Next();
    int chat = found ? relation.rows->GetInt(1) : 0;
    const bool active = found && relation.rows->GetBoolean(2);
    const auto revision = found ? relation.rows->GetInt64(3) : 0;
    if (operation == "delete") {
        if (!active || expected != revision)
            throw messaging::GroupError("VersionConflict");
        Query remove(
            db,
            "DELETE FROM friend WHERE (self_id=? AND other_id=?) OR (self_id=? AND other_id=?)",
            {self, other, other, self},
            false);
        Query deactivate(
            db,
            "UPDATE private_chat SET relationship_active=FALSE,relationship_revision=relationship_revision+1 WHERE chat_id=?",
            {std::to_string(chat)},
            false);
        Query pending(
            db,
            "UPDATE apply_friend SET status=2,revision=revision+1 WHERE "
            "(from_uid=? AND to_uid=?) OR (from_uid=? AND to_uid=?)",
            {self, other, other, self},
            false);
    } else {
        const bool apply = operation == "apply";
        const auto from = apply ? self : other, to = apply ? other : self;
        Query application(
            db,
            "SELECT status,revision,description,backname FROM apply_friend WHERE from_uid=? AND to_uid=? FOR UPDATE",
            {from, to});
        const bool exists = application.rows->Next();
        const auto version = exists ? application.rows->GetInt64(2) : 0;
        if (expected != version || active)
            throw messaging::GroupError("VersionConflict");
        if (apply) {
            ValidateText(db, request["description"], false);
            ValidateText(db, request["backname"], false);
            Query create(
                db,
                database::QueryText{
                    "INSERT INTO apply_friend(from_uid,to_uid,description,backname) VALUES(?,?,?,?) "
                    "ON DUPLICATE KEY UPDATE status=0,revision=revision+1,description=VALUES(description),backname=VALUES(backname)",
                    "INSERT INTO apply_friend(from_uid,to_uid,description,backname) VALUES(?,?,?b,?b) ON CONFLICT "
                    "(from_uid,to_uid) DO UPDATE SET "
                    "status=0,revision=apply_friend.revision+1,description=EXCLUDED.description,backname=EXCLUDED.backname"},
                {from, to, request["description"].asString(), request["backname"].asString()},
                false);
        } else {
            if (!exists || application.rows->GetInt(1) != 0 || (operation != "accept" && operation != "reject"))
                throw messaging::GroupError("VersionConflict");
            const auto description = application.rows->GetString(3);
            const auto applicant_remark = application.rows->GetString(4);
            Query change(
                db,
                "UPDATE apply_friend SET status=?,revision=revision+1 WHERE from_uid=? AND to_uid=?",
                {operation == "accept" ? "1" : "2", from, to},
                false);
            if (operation == "accept") {
                ValidateText(db, request["backname"], false);
                ValidateText(db, request["description"], false);
                if (!chat) {
                    Query create(db, "INSERT INTO chat(type) VALUES('private')", {}, false);
                    Query id(
                        db,
                        database::QueryText{
                            "SELECT LAST_INSERT_ID()",
                            "SELECT currval(pg_get_serial_sequence('chat.chat','chat_id'))"});
                    id.rows->Next();
                    chat = id.rows->GetInt(1);
                    Query pair(
                        db,
                        "INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(?,?,?)",
                        {std::to_string(chat), first, second},
                        false);
                } else {
                    Query activate(
                        db,
                        "UPDATE private_chat SET relationship_active=TRUE,relationship_revision=relationship_revision+1 WHERE chat_id=?",
                        {std::to_string(chat)},
                        false);
                }
                Query clean(
                    db,
                    "DELETE FROM friend WHERE (self_id=? AND other_id=?) OR (self_id=? AND other_id=?)",
                    {self, other, other, self},
                    false);
                Query friends(
                    db,
                    database::QueryText{
                        "INSERT INTO friend(self_id,other_id,backname) VALUES(?,?,?),(?,?,?)",
                        "INSERT INTO friend(self_id,other_id,backname) VALUES(?,?,?b),(?,?,?b)"},
                    {self, other, request["backname"].asString(), other, self, applicant_remark},
                    false);
                Query reverse(
                    db,
                    "UPDATE apply_friend SET status=1,revision=revision+1 WHERE from_uid=? AND to_uid=? AND status=0",
                    {self, other},
                    false);
                for (const auto& entry : std::vector<std::pair<std::string, std::string>>{
                         {other, description},
                         {self, request["description"].asString()}}) {
                    if (entry.second.empty())
                        continue;
                    Query greeting(
                        db,
                        database::QueryText{
                            "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES(?,?,?,?,1)",
                            "INSERT INTO chat_message(chat_id,send_id,recv_id,content,status) VALUES(?,?,?,?b,1)"},
                        {std::to_string(chat), entry.first, entry.first == self ? other : self, entry.second},
                        false);
                }
            }
        }
    }
    response["target_uid"] = peer;
    response["operation"] = operation;
    transaction.Commit();
}
}
