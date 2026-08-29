#ifndef CLIENTSESSION_H
#define CLIENTSESSION_H

#include <QObject>
#include <QPointer>

enum class SessionResetReason {
    Logout,
    SwitchAccount,
    Kicked,
    UnexpectedDisconnect
};

Q_DECLARE_METATYPE(SessionResetReason)

class ClientSession : public QObject
{
    Q_OBJECT

public:
    explicit ClientSession(QObject *parent = nullptr);

    void beginSession(QObject *ownedSessionRoot = nullptr);
    bool resetSession(SessionResetReason reason);
    bool isActive() const;

signals:
    void sessionReset(SessionResetReason reason);

private:
    bool _active = false;
    QPointer<QObject> _ownedSessionRoot;
};

#endif // CLIENTSESSION_H
