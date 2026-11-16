#pragma once
#include "Connection.h"
#include "../../schema/postgresql/SchemaVersion.h"

namespace chat_pg {
// 启动前校验发布版本、实际数据库结构与单行 UID 分配器，失败阻止监听。
inline void Verify(Connection& db) {
    const auto fingerprint = db.Query(FINGERPRINT_SQL);
    if (PQntuples(fingerprint.get()) != 1)
        throw Error("P1003");
    const auto current = db.Query(
        "SELECT (SELECT count(*)=1 AND bool_and(version=1 AND checksum=$1 AND catalog=$2) FROM chat.schema_version) "
        "AND (SELECT count(*)=1 AND min(id)>=coalesce((SELECT max(uid) FROM chat.account),0) FROM chat.user_id)",
        {SCHEMA_HASH, PQgetvalue(fingerprint.get(), 0, 0)});
    if (std::strcmp(PQgetvalue(current.get(), 0, 0), "t") != 0)
        throw Error("P1003");
}
} // namespace chat_pg
