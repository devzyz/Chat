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

    /** @brief 激活认证账号的会话生命周期，后续页面和消息状态属于该账号。 */
    void beginSession(QObject *ownedSessionRoot = nullptr);
    /** @brief 幂等结束账号会话并清理连接、模型及临时账号状态，返回是否执行了重置。 */
    bool resetSession(SessionResetReason reason);
    /** @brief 查询当前是否关联有效账号，不等同于数据库异步打开成功。 */
    bool isActive() const;

signals:
    /** @brief 通知账号会话已按指定原因结束。 */
    void sessionReset(SessionResetReason reason);

private:
    bool _active = false;
    QPointer<QObject> _ownedSessionRoot;
    QTimer _heartbeat;
};

#endif // CLIENTSESSION_H
