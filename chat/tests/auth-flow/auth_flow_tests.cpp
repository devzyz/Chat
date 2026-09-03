#include "authflowcoordinator.h"

#include <QtTest>

class AuthFlowTests : public QObject
{
    Q_OBJECT

private slots:
    void registerNetworkErrorIsReportedOnlyOnce();
    void resetNetworkErrorIsReportedOnlyOnce();
    void loginNetworkErrorIsReportedOnlyOnce();
    void unknownModuleOrRequestDoesNotChangeTheActiveFlow();
    void malformedJsonProducesOneStableError();
    void businessErrorDoesNotConnectChat();
    void loginHttpSuccessConnectsOnlyOnce();
    void tcpFailureDoesNotCreateChat();
    void chatLoginFailureDoesNotShowChat();
    void chatLoginSuccessShowsChatOnlyOnce();
    void duplicateAndLateOldFlowOutcomesAreIgnored();
};

namespace {

AuthAction begin(AuthFlowCoordinator &coordinator, Modules module, ReqId requestId)
{
    AuthOutcome outcome;
    outcome.kind = AuthOutcomeKind::BeginHttp;
    outcome.module = static_cast<int>(module);
    outcome.requestId = static_cast<int>(requestId);
    return coordinator.Reduce(0, outcome);
}

AuthAction loginHttpSuccess(AuthFlowCoordinator &coordinator, AuthFlowId flowId)
{
    ServerInfo server;
    server.Uid = 42;
    server.Host = QStringLiteral("chat.invalid");
    server.Port = QStringLiteral("8090");
    server.Token = QStringLiteral("synthetic-token");
    AuthOutcome success;
    success.kind = AuthOutcomeKind::HttpSuccess;
    success.module = static_cast<int>(Modules::LOGINMOD);
    success.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);
    success.server = server;
    return coordinator.Reduce(flowId, success);
}

} // namespace

void AuthFlowTests::registerNetworkErrorIsReportedOnlyOnce()
{
    AuthFlowCoordinator coordinator;
    const AuthAction started = begin(coordinator, Modules::REGISTERMOD, ReqId::ID_REG_USER);
    QVERIFY(started.accepted);
    QVERIFY(started.flowId != 0);
    QVERIFY(!started.kind.has_value());

    AuthOutcome failure;
    failure.kind = AuthOutcomeKind::HttpNetworkError;
    failure.module = static_cast<int>(Modules::REGISTERMOD);
    failure.requestId = static_cast<int>(ReqId::ID_REG_USER);

    const AuthAction first = coordinator.Reduce(started.flowId, failure);
    QVERIFY(first.accepted);
    QCOMPARE(first.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(first.error, AuthError::Network);

    const AuthAction duplicate = coordinator.Reduce(started.flowId, failure);
    QVERIFY(!duplicate.accepted);
    QVERIFY(!duplicate.kind.has_value());
}

void AuthFlowTests::resetNetworkErrorIsReportedOnlyOnce()
{
    AuthFlowCoordinator coordinator;
    const AuthAction started = begin(coordinator, Modules::RESETMOD, ReqId::ID_RESET_PWD);
    QVERIFY(started.accepted);

    AuthOutcome failure;
    failure.kind = AuthOutcomeKind::HttpNetworkError;
    failure.module = static_cast<int>(Modules::RESETMOD);
    failure.requestId = static_cast<int>(ReqId::ID_RESET_PWD);

    const AuthAction first = coordinator.Reduce(started.flowId, failure);
    QCOMPARE(first.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(first.error, AuthError::Network);
    QVERIFY(!coordinator.Reduce(started.flowId, failure).kind.has_value());
}

void AuthFlowTests::loginNetworkErrorIsReportedOnlyOnce()
{
    AuthFlowCoordinator coordinator;
    const AuthAction started = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    QVERIFY(started.accepted);

    AuthOutcome failure;
    failure.kind = AuthOutcomeKind::HttpNetworkError;
    failure.module = static_cast<int>(Modules::LOGINMOD);
    failure.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);

    const AuthAction first = coordinator.Reduce(started.flowId, failure);
    QCOMPARE(first.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(first.error, AuthError::Network);
    QVERIFY(!coordinator.Reduce(started.flowId, failure).kind.has_value());
}

void AuthFlowTests::unknownModuleOrRequestDoesNotChangeTheActiveFlow()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);

    AuthOutcome unknown;
    unknown.kind = AuthOutcomeKind::HttpNetworkError;
    unknown.module = 99;
    unknown.requestId = 9999;
    const AuthAction ignored = coordinator.Reduce(active.flowId, unknown);
    QVERIFY(!ignored.accepted);
    QVERIFY(!ignored.kind.has_value());

    AuthOutcome valid;
    valid.kind = AuthOutcomeKind::HttpNetworkError;
    valid.module = static_cast<int>(Modules::LOGINMOD);
    valid.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);
    QCOMPARE(coordinator.Reduce(active.flowId, valid).kind,
             std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));

    AuthOutcome unknownBegin;
    unknownBegin.kind = AuthOutcomeKind::BeginHttp;
    unknownBegin.module = 99;
    unknownBegin.requestId = 9999;
    QVERIFY(!coordinator.Reduce(0, unknownBegin).accepted);
}

void AuthFlowTests::malformedJsonProducesOneStableError()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    AuthOutcome malformed;
    malformed.kind = AuthOutcomeKind::HttpMalformedJson;
    malformed.module = static_cast<int>(Modules::LOGINMOD);
    malformed.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);

    const AuthAction first = coordinator.Reduce(active.flowId, malformed);
    QCOMPARE(first.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(first.error, AuthError::MalformedResponse);
    QVERIFY(!coordinator.Reduce(active.flowId, malformed).accepted);
}

void AuthFlowTests::businessErrorDoesNotConnectChat()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    AuthOutcome failure;
    failure.kind = AuthOutcomeKind::HttpBusinessError;
    failure.module = static_cast<int>(Modules::LOGINMOD);
    failure.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);
    failure.businessError = 17;

    const AuthAction action = coordinator.Reduce(active.flowId, failure);
    QCOMPARE(action.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(action.error, AuthError::Business);
    QVERIFY(action.kind != std::optional<AuthActionKind>(AuthActionKind::ConnectChat));
}

void AuthFlowTests::loginHttpSuccessConnectsOnlyOnce()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    ServerInfo server;
    server.Uid = 42;
    server.Host = QStringLiteral("chat.invalid");
    server.Port = QStringLiteral("8090");
    server.Token = QStringLiteral("synthetic-token");
    AuthOutcome success;
    success.kind = AuthOutcomeKind::HttpSuccess;
    success.module = static_cast<int>(Modules::LOGINMOD);
    success.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);
    success.server = server;

    const AuthAction first = coordinator.Reduce(active.flowId, success);
    QCOMPARE(first.kind, std::optional<AuthActionKind>(AuthActionKind::ConnectChat));
    QVERIFY(first.server.has_value());
    QCOMPARE(first.server->Uid, 42);
    QCOMPARE(first.server->Host, QStringLiteral("chat.invalid"));
    QVERIFY(!coordinator.Reduce(active.flowId, success).accepted);
}

void AuthFlowTests::tcpFailureDoesNotCreateChat()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    QCOMPARE(loginHttpSuccess(coordinator, active.flowId).kind,
             std::optional<AuthActionKind>(AuthActionKind::ConnectChat));

    AuthOutcome failure;
    failure.kind = AuthOutcomeKind::TcpConnectFailed;
    const AuthAction action = coordinator.Reduce(active.flowId, failure);
    QCOMPARE(action.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(action.error, AuthError::TcpConnection);
    QVERIFY(action.kind != std::optional<AuthActionKind>(AuthActionKind::ShowChat));
}

void AuthFlowTests::chatLoginFailureDoesNotShowChat()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    loginHttpSuccess(coordinator, active.flowId);
    AuthOutcome connected;
    connected.kind = AuthOutcomeKind::TcpConnected;
    const AuthAction connection = coordinator.Reduce(active.flowId, connected);
    QVERIFY(connection.accepted);
    QVERIFY(!connection.kind.has_value());

    AuthOutcome failure;
    failure.kind = AuthOutcomeKind::ChatLoginFailed;
    failure.businessError = 23;
    const AuthAction action = coordinator.Reduce(active.flowId, failure);
    QCOMPARE(action.kind, std::optional<AuthActionKind>(AuthActionKind::StayAndShowError));
    QCOMPARE(action.error, AuthError::ChatLogin);
    QVERIFY(action.kind != std::optional<AuthActionKind>(AuthActionKind::ShowChat));
}

void AuthFlowTests::chatLoginSuccessShowsChatOnlyOnce()
{
    AuthFlowCoordinator coordinator;
    const AuthAction active = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    loginHttpSuccess(coordinator, active.flowId);
    AuthOutcome connected;
    connected.kind = AuthOutcomeKind::TcpConnected;
    QVERIFY(coordinator.Reduce(active.flowId, connected).accepted);

    AuthOutcome success;
    success.kind = AuthOutcomeKind::ChatLoginSucceeded;
    const AuthAction first = coordinator.Reduce(active.flowId, success);
    QCOMPARE(first.kind, std::optional<AuthActionKind>(AuthActionKind::ShowChat));
    const AuthAction duplicate = coordinator.Reduce(active.flowId, success);
    QVERIFY(!duplicate.accepted);
    QVERIFY(!duplicate.kind.has_value());
}

void AuthFlowTests::duplicateAndLateOldFlowOutcomesAreIgnored()
{
    AuthFlowCoordinator coordinator;
    const AuthAction oldFlow = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    const AuthAction currentFlow = begin(coordinator, Modules::LOGINMOD, ReqId::ID_LOGIN_UESR);
    QVERIFY(currentFlow.flowId > oldFlow.flowId);

    const AuthAction late = loginHttpSuccess(coordinator, oldFlow.flowId);
    QVERIFY(!late.accepted);
    QVERIFY(!late.kind.has_value());

    const AuthAction current = loginHttpSuccess(coordinator, currentFlow.flowId);
    QCOMPARE(current.kind, std::optional<AuthActionKind>(AuthActionKind::ConnectChat));
    const AuthAction duplicate = loginHttpSuccess(coordinator, currentFlow.flowId);
    QVERIFY(!duplicate.accepted);
    QVERIFY(!duplicate.kind.has_value());
}

QTEST_APPLESS_MAIN(AuthFlowTests)

#include "auth_flow_tests.moc"
