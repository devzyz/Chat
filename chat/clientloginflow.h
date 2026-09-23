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
    /** @brief 取消当前认证流程及其网络请求。 */
    ~ClientLoginFlow() override;
    /** @brief 发起新的登录流程并返回流程 ID，替换当前未完成尝试。 */
    AuthFlowId login(const QUrl &gate, const QString &email, const QString &password);
    /** @brief 取消当前认证流程及其 HTTP 请求和超时任务，迟到结果不再推进该流程。 */
    void cancel();
    /** @brief 返回本次选服获得的服务器主机名。 */
    QString serverHost() const { return _server.Host; }
    /** @brief 返回本次选服获得的服务器端口。 */
    quint16 serverPort() const { return _server.Port.toUShort(); }

signals:
    /** @brief 通知指定认证流程失败，携带结构化错误供界面翻译。 */
    void failed(AuthFlowId flowId, AuthError error);
    /** @brief 通知当前连接已建立，尚不表示业务认证成功。 */
    void connected();
    /** @brief 通知该流程的聊天登录已确认，可以建立账号会话。 */
    void authenticated(AuthFlowId flowId);

private:
    /** @brief 推进认证结果并发起后续连接或登录完成通知。 */
    void apply(const AuthOutcome &outcome);
    /** @brief 结束当前登录尝试并向认证协调器及界面报告错误。 */
    void finishError(AuthError error);
    AuthFlowCoordinator &_coordinator;
    GateHttpTransport _http;
    QTimer _deadline;
    AuthFlowId _flowId = 0;
    ServerInfo _server{};
    bool _pending = false;
};
