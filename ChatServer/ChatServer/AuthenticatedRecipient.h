#pragma once
#include "CSession.h"
#include "UserPresenceStore.h"
#include "UserSessionDirectory.h"

// 在调用线程复核原连接的 Redis 绑定和凭据，失效或无法确认时关闭该连接。
inline bool RefreshAuthenticatedSession(
    UserPresenceStore& presence,
    int uid,
    const std::shared_ptr<CSession>& session) {
    try {
        if (presence.RefreshIfCurrent(uid, session->Id()))
            return !session->Closing();
    } catch (...) {
        // 凭据存储异常与校验失败共用关闭路径。
    }
    session->Close(SessionCloseReason::Replaced);
    return false;
}

/** @brief 在业务或 RPC 工作线程验证推送接收者仍持有当前凭据；失效或存储异常时关闭并拒绝推送。 */
inline std::shared_ptr<CSession> FindAuthorizedRecipient(
    UserSessionDirectory& directory,
    UserPresenceStore& presence,
    int uid) {
    auto session = directory.FindCurrent(uid);
    if (session && RefreshAuthenticatedSession(presence, uid, session))
        return session;
    return {};
}
