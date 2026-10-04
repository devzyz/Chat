#ifndef CLIENTSESSION_H
#define CLIENTSESSION_H

#include <QObject>
#include <QPointer>
#include <QTimer>
#include "gatehttptransport.h"
#include "userdata.h"

enum class SessionResetReason {
    Logout,
    SwitchAccount,
    Kicked,
    UnexpectedDisconnect
};

Q_DECLARE_METATYPE(SessionResetReason)

/** @brief 管理已认证会话的心跳及账号、连接和会话界面的统一清理。 */
class ClientSession : public QObject
{
    Q_OBJECT

public:
    /** @brief 创建心跳定时器，将当前账号心跳交给 TCP 管理器发送。 */
    explicit ClientSession(QObject *parent = nullptr);

    /** @brief 激活认证账号的会话生命周期，后续页面和消息状态属于该账号。 */
    void beginSession(QObject *ownedSessionRoot = nullptr);
    /** @brief 幂等结束账号会话并清理连接、模型及临时账号状态，返回是否执行了重置。 */
    bool resetSession(SessionResetReason reason);
    /** @brief 查询当前是否关联有效账号，不等同于数据库异步打开成功。 */
    bool isActive() const;
    /** @brief 异步撤销当前凭据；失败保留会话供重试，完成由 logoutFinished 通知。 */
    bool requestLogout(SessionResetReason reason);
    /** @brief 查询是否正在恢复同账号连接，期间保留会话界面及草稿。 */
    bool isReconnecting() const { return _reconnecting; }
    /** @brief 查询是否正在等待撤销确认，期间断线不得抢先清理界面。 */
    bool isLoggingOut() const { return _loggingOut; }

signals:
    /** @brief 通知账号会话已按指定原因结束。 */
    void sessionReset(SessionResetReason reason);
    /** @brief 返回服务器撤销结果；成功时本地账号已清理，失败时允许重试。 */
    void logoutFinished(bool success);
    /** @brief 自动恢复被拒绝或耗尽有限尝试，需要返回登录页。 */
    void reconnectFailed();
    /** @brief 通知自动恢复状态，便于界面显示连接提示。 */
    void reconnectChanged(bool active);

private:
    /** @brief 调度一次有上限的退避重连；不保存密码或创建新登录 Token。 */
    void scheduleReconnect();
    /** @brief 停止恢复计时器并撤销待进行的恢复动作。 */
    void stopReconnect();
    bool _active = false;
    QPointer<QObject> _ownedSessionRoot;
    QTimer _heartbeat;
    QTimer _heartbeatDeadline;
    QTimer _retry;
    QTimer _attemptDeadline;
    GateHttpTransport _logout;
    ServerInfo _resumeServer{};
    quint64 _logoutGeneration = 0;
    int _attempts = 0;
    bool _reconnecting = false;
    bool _loggingOut = false;
    bool _authenticationRejected = false;
    SessionResetReason _logoutReason = SessionResetReason::Logout;
};

#endif // CLIENTSESSION_H
