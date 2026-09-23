#pragma once

#include "authflowcoordinator.h"
#include "gatehttptransport.h"
#include <QObject>
#include <QTimer>

/** @brief 协调单次 HTTP 选服与 TCP 登录，连接由 TcpMgr 持有。 */
class ClientLoginFlow final : public QObject
{
    Q_OBJECT
public:
    /** @brief 接入认证协调器并连接 HTTP、TCP 与超时结果。 */
    explicit ClientLoginFlow(AuthFlowCoordinator &coordinator, QObject *parent = nullptr);
    ~ClientLoginFlow() override;
    /** @brief 发起新的登录流程并返回流程 ID，替换当前未完成尝试。 */
    AuthFlowId login(const QUrl &gate, const QString &email, const QString &password);
    void cancel();
    QString serverHost() const { return _server.Host; }
    quint16 serverPort() const { return _server.Port.toUShort(); }

signals:
    void failed(AuthFlowId flowId, AuthError error);
    void connected();
    void authenticated(AuthFlowId flowId);

private:
    /** @brief 推进认证结果并发起后续连接或登录完成通知。 */
    void apply(const AuthOutcome &outcome);
    void finishError(AuthError error);
    AuthFlowCoordinator &_coordinator;
    GateHttpTransport _http;
    QTimer _deadline;
    AuthFlowId _flowId = 0;
    ServerInfo _server{};
    bool _pending = false;
};
