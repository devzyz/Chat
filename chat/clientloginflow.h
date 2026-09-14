#pragma once

#include "authflowcoordinator.h"
#include "gatehttptransport.h"
#include <QObject>
#include <QTimer>

// One login flow per client process; TcpMgr owns the production TCP session.
class ClientLoginFlow final : public QObject
{
    Q_OBJECT
public:
    explicit ClientLoginFlow(AuthFlowCoordinator &coordinator, QObject *parent = nullptr);
    ~ClientLoginFlow() override;
    AuthFlowId login(const QUrl &gate, const QString &email, const QString &password);
    void cancel();
    QString serverHost() const { return _server.Host; }
    quint16 serverPort() const { return _server.Port.toUShort(); }

signals:
    void failed(AuthFlowId flowId, AuthError error);
    void connected();
    void authenticated(AuthFlowId flowId);

private:
    void apply(const AuthOutcome &outcome);
    void finishError(AuthError error);
    AuthFlowCoordinator &_coordinator;
    GateHttpTransport _http;
    QTimer _deadline;
    AuthFlowId _flowId = 0;
    ServerInfo _server{};
    bool _pending = false;
};
