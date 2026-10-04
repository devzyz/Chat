#include "clientsession.h"

#include "tcpmgr.h"
#include "usermgr.h"
#include <QJsonDocument>

ClientSession::ClientSession(QObject *parent)
    : QObject(parent)
{
    _retry.setSingleShot(true); _attemptDeadline.setSingleShot(true);
    connect(&_retry, &QTimer::timeout, this, /** @brief 每次恢复使用原账号、凭据和端点，整次认证限时十秒。 */ [this] {
        if (!_active || !_reconnecting || _loggingOut) return;
        ++_attempts; _attemptDeadline.start(10000);
        TcpMgr::instance()->connectToServer(_resumeServer);
    });
    connect(&_attemptDeadline, &QTimer::timeout, this, /** @brief 无认证响应时结束旧连接并安排下一次有限尝试。 */ [this] {
        TcpMgr::instance()->resetConnection(false); scheduleReconnect();
    });
    const auto tcp = TcpMgr::instance();
    _heartbeatDeadline.setSingleShot(true);
    _heartbeatDeadline.setInterval(30000);
    connect(&_heartbeatDeadline, &QTimer::timeout, this,
        /** @brief TCP 可写但心跳迟迟未确认时关闭旧连接，进入同账号有限重连。 */ [this] {
        if (!_active || _loggingOut || _reconnecting || _authenticationRejected) return;
        TcpMgr::instance()->resetConnection(false); scheduleReconnect();
    });
    connect(tcp.get(), &TcpMgr::heartbeatAcknowledged, this,
        /** @brief 仅当前活跃认证连接的有效回复解除本轮心跳期限。 */ [this] {
        if (_active && !_reconnecting && !_loggingOut) _heartbeatDeadline.stop();
    });
    connect(tcp.get(), &TcpMgr::connectionClosed, this, /** @brief 暂时断线保持账号界面，恢复仍使用同一 Token。 */ [this](bool expected) {
        if (!expected) scheduleReconnect();
    });
    connect(tcp.get(), &TcpMgr::connectionAttemptFinished, this, /** @brief 恢复连接成功后重新认证，连接失败进入退避。 */ [this](bool success) {
        if (!_active || !_reconnecting || _loggingOut) return;
        if (!success) { scheduleReconnect(); return; }
        emit TcpMgr::instance()->sendRequested(ID_CHAT_LOGIN_REQ,
            QJsonDocument(QJsonObject{{"uid", _resumeServer.Uid}, {"token", _resumeServer.Token}}).toJson(QJsonDocument::Compact));
    });
    connect(tcp.get(), &TcpMgr::loginSucceeded, this, /** @brief 恢复认证成功后停止退避并重新开始心跳。 */ [this] {
        if (!_active || !_reconnecting) return;
        stopReconnect(); _attempts = 0; _heartbeatDeadline.stop(); _heartbeat.start();
    });
    connect(tcp.get(), &TcpMgr::loginFailed, this, /** @brief 失效凭据停止自动尝试，服务暂时失败保留有限重试。 */ [this](int error) {
        if (!_active || !_reconnecting) return;
        if (error == 1010 || error == 1011) {
            _authenticationRejected = true; stopReconnect(); emit reconnectFailed();
        } else { TcpMgr::instance()->resetConnection(false); scheduleReconnect(); }
    });
    connect(tcp.get(), &TcpMgr::forcedOffline, this, /** @brief 被替换的账号不得通过自动重连踢回新会话。 */ [this] {
        _authenticationRejected = true; stopReconnect(); _heartbeat.stop(); _heartbeatDeadline.stop();
    });
    connect(&_logout, &GateHttpTransport::finished, this, /** @brief 仅接受当前退出操作，撤销确认前保留账号状态。 */ [this](const GateHttpResult& result) {
        if (!_loggingOut || result.flowId != _logoutGeneration) return;
        _loggingOut = false;
        const auto value = QJsonDocument::fromJson(result.body).object();
        const auto error = value.value("error");
        const bool success = result.terminal == GateHttpTerminal::Success && error.isDouble()
            && (error.toInt(-1) == 0 || error.toInt(-1) == 1011);
        if (success) resetSession(_logoutReason);
        else if (!TcpMgr::instance()->isAuthenticated()) scheduleReconnect();
        else _heartbeat.start();
        emit logoutFinished(success);
    });
    _heartbeat.setInterval(10000);
    connect(&_heartbeat, &QTimer::timeout, this,
        /** @brief 只为仍活跃且有效的账号发送心跳。 */
        [this] {
        const int uid = UserMgr::instance()->uid();
        if (!_active || _loggingOut || _reconnecting || uid <= 0) return;
        if (!_heartbeatDeadline.isActive()) _heartbeatDeadline.start();
        emit TcpMgr::instance()->sendRequested(ID_HEART_BEAT_REQ,
            QJsonDocument(QJsonObject{{"uid", uid}}).toJson(QJsonDocument::Compact));
    });
}

void ClientSession::beginSession(QObject *ownedSessionRoot)
{
    _ownedSessionRoot = ownedSessionRoot;
    _active = true;
    _authenticationRejected = false; _attempts = 0;
    _resumeServer = TcpMgr::instance()->connectionInfo();
    _heartbeatDeadline.stop();
    _heartbeat.start();
    TcpMgr::instance()->beginSession();
}

bool ClientSession::resetSession(SessionResetReason reason)
{
    if (!_active) {
        return false;
    }

    _active = false;
    _loggingOut = false; ++_logoutGeneration; _logout.reset();
    stopReconnect(); _resumeServer = {};
    _heartbeat.stop();
    _heartbeatDeadline.stop();
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

bool ClientSession::requestLogout(SessionResetReason reason)
{
    if (!_active || _loggingOut || (reason != SessionResetReason::Logout && reason != SessionResetReason::SwitchAccount)) return false;
    _loggingOut = true; _logoutReason = reason; stopReconnect();
    _heartbeat.stop(); _heartbeatDeadline.stop();
    GateHttpRequest request;
    request.url = QUrl(gate_url_prefix).resolved(QUrl("/logout"));
    request.body = QJsonDocument(QJsonObject{{"uid", UserMgr::instance()->uid()},
        {"token", UserMgr::instance()->token()}}).toJson(QJsonDocument::Compact);
    request.flowId = ++_logoutGeneration; request.requestId = 0; request.module = 0; request.deadlineMs = 5000;
    _logout.post(request);
    return true;
}

void ClientSession::stopReconnect()
{
    _retry.stop(); _attemptDeadline.stop();
    if (_reconnecting) { _reconnecting = false; emit reconnectChanged(false); }
}

void ClientSession::scheduleReconnect()
{
    if (!_active || _loggingOut || _authenticationRejected || _retry.isActive()) return;
    _attemptDeadline.stop(); _heartbeat.stop(); _heartbeatDeadline.stop();
    if (_attempts >= 5 || _resumeServer.Uid <= 0 || _resumeServer.Token.isEmpty() || _resumeServer.Host.isEmpty()) {
        stopReconnect(); emit reconnectFailed(); return;
    }
    if (!_reconnecting) { _reconnecting = true; emit reconnectChanged(true); }
    _retry.start(1000 * (1 << _attempts));
}
