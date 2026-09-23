#pragma once
#include "SessionTypes.h"
#include <memory>
#include <mutex>
#include <unordered_map>

class CSession;
/** @brief 加锁维护用户的当前会话弱引用，不延长会话生命周期。 */
class UserSessionDirectory {
public:
    /** @brief 替换 uid 的映射，返回仍存活的旧会话；没有旧会话时返回 nullptr，不负责关闭旧会话。 */
    std::shared_ptr<CSession> Register(int uid, const SessionId& id, std::weak_ptr<CSession> session);
    /** @brief 返回当前会话的强引用；不存在或已过期时返回 nullptr，返回后映射仍可能被替换。 */
    std::shared_ptr<CSession> FindCurrent(int uid) const;
    /** @brief 判断 id 是否匹配 uid 的当前存活会话；结果仅代表加锁查询时刻。 */
    bool IsCurrent(int uid, const SessionId& id) const;
    /** @brief 仅删除仍匹配 id 的映射，旧会话关闭不会删除后来登记的会话。 */
    void UnregisterIfCurrent(int uid, const SessionId& id);
private:
    /** @brief 保存用于条件删除的会话标识及不拥有会话的弱引用。 */
    struct Entry { SessionId id; std::weak_ptr<CSession> session; };
    mutable std::mutex _mutex;
    std::unordered_map<int, Entry> _entries;
};
