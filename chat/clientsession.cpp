#include "clientsession.h"

#include "tcpmgr.h"
#include "usermgr.h"
#include <QJsonDocument>

ClientSession::ClientSession(QObject *parent)
    : QObject(parent)
{
    _heartbeat.setInterval(10000);
    connect(&_heartbeat, &QTimer::timeout, this, [this] {
        const int uid = UserMgr::GetInstance()->GetUid();
        if (!_active || uid <= 0) return;
        emit TcpMgr::GetInstance()->sig_send_data(ID_HEART_BEAT_REQ,
            QJsonDocument(QJsonObject{{"uid", uid}}).toJson(QJsonDocument::Compact));
    });
}

void ClientSession::beginSession(QObject *ownedSessionRoot)
{
    _ownedSessionRoot = ownedSessionRoot;
    _active = true;
    _heartbeat.start();
    TcpMgr::GetInstance()->beginSession();
}

bool ClientSession::resetSession(SessionResetReason reason)
{
    if (!_active) {
        return false;
    }

    _active = false;
    _heartbeat.stop();
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
