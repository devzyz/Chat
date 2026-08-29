#include "clientsession.h"

#include "tcpmgr.h"
#include "usermgr.h"

ClientSession::ClientSession(QObject *parent)
    : QObject(parent)
{
}

void ClientSession::beginSession(QObject *ownedSessionRoot)
{
    _ownedSessionRoot = ownedSessionRoot;
    _active = true;
    TcpMgr::GetInstance()->beginSession();
}

bool ClientSession::resetSession(SessionResetReason reason)
{
    if (!_active) {
        return false;
    }

    _active = false;
    TcpMgr::GetInstance()->resetConnection(
        reason != SessionResetReason::UnexpectedDisconnect);
    UserMgr::GetInstance()->resetSession();
    if (_ownedSessionRoot) {
        QObject *ownedSessionRoot = _ownedSessionRoot.data();
        _ownedSessionRoot.clear();
        delete ownedSessionRoot;
    }
    emit sessionReset(reason);
    return true;
}

bool ClientSession::isActive() const
{
    return _active;
}
