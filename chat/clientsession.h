#ifndef CLIENTSESSION_H
#define CLIENTSESSION_H

#include <QObject>
#include <QPointer>
#include <QTimer>

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

    void beginSession(QObject *ownedSessionRoot = nullptr);
    bool resetSession(SessionResetReason reason);
    bool isActive() const;

signals:
    void sessionReset(SessionResetReason reason);

private:
    bool _active = false;
    QPointer<QObject> _ownedSessionRoot;
    QTimer _heartbeat;
};

#endif // CLIENTSESSION_H
