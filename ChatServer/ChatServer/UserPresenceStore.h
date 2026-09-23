#pragma once
#include "SessionTypes.h"
#include <optional>

namespace chat_session {
/** @brief 标识用户所在实例及具体会话，用于防止旧连接误删或误踢新连接。 */
struct UserPresence { std::string server_id; SessionId session_id; };
}
enum class PresenceStatus { Found, NotFound, Unavailable };
/** @brief 区分查得位置、不存在与存储不可用；Publish 的位置表示被替换的旧值。 */
struct PresenceResult {
    PresenceStatus status = PresenceStatus::Unavailable;
    std::optional<chat_session::UserPresence> presence;
};
/** @brief 提供同步在线位置存取及条件删除；调用可能阻塞，由生命周期工作线程隔离。 */
class UserPresenceStore {
public:
    /** @brief 允许通过存储接口销毁具体适配器。 */
    virtual ~UserPresenceStore() = default;
    /** @brief 原子发布新位置并返回旧位置；首次成功为 NotFound，存储失败为 Unavailable。 */
    virtual PresenceResult Publish(int uid, const chat_session::UserPresence& presence) = 0;
    /** @brief 同步读取位置；缺失为 NotFound，失败为 Unavailable，适配器异常由调用方处理。 */
    virtual PresenceResult Find(int uid) = 0;
    /** @brief 仅在实例和会话都匹配时删除；不匹配也返回 true，false 表示存储失败。 */
    virtual bool RemoveIfCurrent(int uid, const chat_session::UserPresence& presence) = 0;
};
