#pragma once
#include "MessagePersistence.h"

namespace messaging {
/** @brief 私聊新增写入的关系拒绝，与数据库故障分离。 */
class PrivateSendDenied : public std::runtime_error {
public:
    /** @brief 构造固定安全错误，不携带数据库或用户内容。 */
    PrivateSendDenied() : std::runtime_error("InvalidMembership") {}
};
/** @brief 在已持有私聊事务锁时核对新增写入资格；旧客户端只允许初始关系版本。 */
inline void CheckPrivateSend(sql::Connection& db, int chat, std::int64_t revision) {
    std::unique_ptr<sql::PreparedStatement> statement(db.prepareStatement(
        "SELECT relationship_active,relationship_revision FROM private_chat WHERE chat_id=? FOR UPDATE"));
    statement->setInt(1, chat);
    std::unique_ptr<sql::ResultSet> row(statement->executeQuery());
    if (!row->next() || !row->getBoolean(1)
        || row->getInt64(2) != (revision > 0 ? revision : 1)) throw PrivateSendDenied();
}
}
