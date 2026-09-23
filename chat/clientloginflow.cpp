#include "clientloginflow.h"
#include "tcpmgr.h"
#include <QJsonDocument>

ClientLoginFlow::ClientLoginFlow(AuthFlowCoordinator &coordinator, QObject *parent)
    : QObject(parent), _coordinator(coordinator)
{
    _deadline.setSingleShot(true);
    // 登录总期限到达时结束当前尝试。
    connect(&_deadline, &QTimer::timeout, this,
        /** @brief 登录流程超时后报告网络失败。 */
        [this] { finishError(AuthError::Network); });
    // 将选服 HTTP 结果转换为认证流程事件。
    connect(&_http, &GateHttpTransport::finished, this,
        /** @brief 仅处理当前待完成流程的 HTTP 选服结果。 */
        [this](const GateHttpResult &result) {
        if (!_pending || result.flowId != _flowId) return;
        AuthOutcome outcome;
        outcome.module = Modules::LOGINMOD;
        outcome.requestId = ReqId::ID_LOGIN_UESR;
        outcome.kind = AuthOutcomeKind::HttpNetworkError;
        if (result.terminal == GateHttpTerminal::MalformedResponse) {
            outcome.kind = AuthOutcomeKind::HttpMalformedJson;
        } else if (result.terminal == GateHttpTerminal::Success) {
            const auto object = QJsonDocument::fromJson(result.body).object();
            if (!object.value("error").isDouble()) {
                outcome.kind = AuthOutcomeKind::HttpMalformedJson;
            } else if (object.value("error").toInt(-1) != 0) {
                outcome.kind = AuthOutcomeKind::HttpBusinessError;
                outcome.businessError = object.value("error").toInt(-1);
            } else {
                ServerInfo server{};
                server.Uid = object.value("uid").toInt();
                server.Host = object.value("host").toString();
                server.Port = object.value("port").toString();
                server.Token = object.value("token").toString();
                bool validPort = false;
                const auto port = server.Port.toUShort(&validPort);
                if (server.Uid <= 0 || server.Host.isEmpty() || !validPort || port == 0 || server.Token.isEmpty()) {
                    outcome.kind = AuthOutcomeKind::HttpMalformedJson;
                } else {
                    outcome.kind = AuthOutcomeKind::HttpSuccess;
                    outcome.server = server;
                }
            }
        }
        apply(outcome);
    });
    const auto tcp = TcpMgr::instance();
    // 连接成功且流程仍有效时发送聊天登录请求。
    connect(tcp.get(), &TcpMgr::connectionAttemptFinished, this,
        /** @brief 将连接结果交给认证协调器并决定是否发送聊天登录。 */
        [this](bool success) {
        if (!_pending) return;
        AuthOutcome outcome;
        outcome.kind = success ? AuthOutcomeKind::TcpConnected : AuthOutcomeKind::TcpConnectFailed;
        const auto action = _coordinator.reduce(_flowId, outcome);
        if (action.kind == AuthActionKind::StayAndShowError) {
            finishError(action.error);
        } else if (success && action.accepted) {
            emit connected();
            emit TcpMgr::instance()->sendRequested(ReqId::ID_CHAT_LOGIN_REQ,
                QJsonDocument(QJsonObject{{"uid", _server.Uid}, {"token", _server.Token}})
                    .toJson(QJsonDocument::Compact));
        }
    });
    // 将聊天登录拒绝交给认证流程处理。
    connect(tcp.get(), &TcpMgr::loginFailed, this,
        /** @brief 将聊天登录错误映射为认证失败结果。 */
        [this](int error) {
        AuthOutcome outcome;
        outcome.kind = AuthOutcomeKind::ChatLoginFailed;
        outcome.businessError = error;
        apply(outcome);
    });
    // 将聊天登录成功交给认证流程处理。
    connect(tcp.get(), &TcpMgr::loginSucceeded, this,
        /** @brief 将聊天登录成功推进为认证完成。 */
        [this] {
        AuthOutcome outcome;
        outcome.kind = AuthOutcomeKind::ChatLoginSucceeded;
        apply(outcome);
    });
}

ClientLoginFlow::~ClientLoginFlow() { cancel(); }

AuthFlowId ClientLoginFlow::login(const QUrl &gate, const QString &email, const QString &password)
{
    cancel();
    AuthOutcome begin;
    begin.kind = AuthOutcomeKind::BeginHttp;
    begin.module = Modules::LOGINMOD;
    begin.requestId = ReqId::ID_LOGIN_UESR;
    _flowId = _coordinator.reduce(0, begin).flowId;
    _pending = true;
    _server = {};
    GateHttpRequest request;
    request.url = gate.resolved(QUrl("/user_login"));
    request.body = QJsonDocument(QJsonObject{{"email", email}, {"password", xorString(password)}})
                       .toJson(QJsonDocument::Compact);
    request.flowId = _flowId;
    request.module = Modules::LOGINMOD;
    request.requestId = ReqId::ID_LOGIN_UESR;
    request.deadlineMs = 5000;
    _deadline.start(10000);
    _http.post(request);
    return _flowId;
}

void ClientLoginFlow::cancel()
{
    const bool pending = _pending;
    _pending = false;
    _deadline.stop();
    _http.reset();
    _server.Token.clear();
    if (pending) TcpMgr::instance()->resetConnection(true);
}

void ClientLoginFlow::finishError(AuthError error)
{
    if (!_pending) return;
    cancel();
    emit failed(_flowId, error);
}

void ClientLoginFlow::apply(const AuthOutcome &outcome)
{
    if (!_pending) return;
    const auto action = _coordinator.reduce(_flowId, outcome);
    if (action.kind == AuthActionKind::StayAndShowError) {
        finishError(action.error);
    } else if (action.kind == AuthActionKind::ConnectChat && action.server) {
        _server = *action.server;
        TcpMgr::instance()->connectToServer(_server);
    } else if (action.kind == AuthActionKind::ShowChat) {
        _pending = false;
        _deadline.stop();
        _server.Token.clear();
        emit authenticated(_flowId);
    }
}
