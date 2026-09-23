#include "clientsession.h"

#include "tcpmgr.h"
#include "usermgr.h"
#include <QJsonDocument>

ClientSession::ClientSession(QObject *parent)
    : QObject(parent)
{
    _heartbeat.setInterval(10000);
    connect(&_heartbeat, &QTimer::timeout, this,
        /** @brief 只为仍活跃且有效的账号发送心跳。 */
        [this] {
        const int uid = UserMgr::instance()->uid();
        if (!_active || uid <= 0) return;
        emit TcpMgr::instance()->sendRequested(ID_HEART_BEAT_REQ,
            QJsonDocument(QJsonObject{{"uid", uid}}).toJson(QJsonDocument::Compact));
    });
}

void ClientSession::beginSession(QObject *ownedSessionRoot)
{
    _ownedSessionRoot = ownedSessionRoot;
    _active = true;
    _heartbeat.start();
    TcpMgr::instance()->beginSession();
}

bool ClientSession::resetSession(SessionResetReason reason)
{
    if (!_active) {
        return false;
    }

    _active = false;
    _heartbeat.stop();
    TcpMgr::instance()->resetConnection(
        reason != SessionResetReason::UnexpectedDisconnect);
    UserMgr::instance()->resetSession();
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
