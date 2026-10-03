#pragma once
#include "CSession.h"
#include "UserPresenceStore.h"
#include "UserSessionDirectory.h"

/** @brief 在业务或 RPC 工作线程验证推送接收者仍持有当前凭据；失效或存储异常时关闭并拒绝推送。 */
inline std::shared_ptr<CSession> FindAuthorizedRecipient(UserSessionDirectory& directory,
    UserPresenceStore& presence, int uid) {
    auto session = directory.FindCurrent(uid);
    if (!session) return {};
    try {
        if (presence.RefreshIfCurrent(uid, session->Id())) return session;
    } catch (...) {
        // 在线身份无法确认时保持失败关闭，禁止把业务内容交给旧凭据。
    }
    session->Close(SessionCloseReason::Replaced);
    return {};
}
