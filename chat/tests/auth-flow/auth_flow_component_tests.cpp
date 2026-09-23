#include "authflowcoordinator.h"
#include "clientsession.h"
#include "tcpmgr.h"
#include "usermgr.h"

#include <QSignalSpy>
#include <QtTest>
#include <spdlog/sinks/null_sink.h>

/** @brief 验证认证动作与真实客户端会话清理的接线。 */
class AuthFlowComponentTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    /** @brief 验证异常断线动作委托会话对象清理账号状态。 */
    void abnormalDisconnectDelegatesToClientSessionReset();
};

void AuthFlowComponentTests::initTestCase()
{
    qRegisterMetaType<SessionResetReason>("SessionResetReason");
    spdlog::set_default_logger(
        spdlog::create<spdlog::sinks::null_sink_mt>("auth-flow-component-tests"));
}

void AuthFlowComponentTests::cleanupTestCase()
{
    TcpMgr::ReleaseInstance();
    UserMgr::ReleaseInstance();
}

void AuthFlowComponentTests::abnormalDisconnectDelegatesToClientSessionReset()
{
    AuthFlowCoordinator coordinator;
    AuthOutcome begin;
    begin.kind = AuthOutcomeKind::BeginHttp;
    begin.module = static_cast<int>(Modules::LOGINMOD);
    begin.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);
    const AuthAction flow = coordinator.reduce(0, begin);

    ServerInfo server;
    server.Uid = 42;
    server.Host = QStringLiteral("chat.invalid");
    server.Port = QStringLiteral("8090");
    server.Token = QStringLiteral("synthetic-token");
    AuthOutcome http;
    http.kind = AuthOutcomeKind::HttpSuccess;
    http.module = static_cast<int>(Modules::LOGINMOD);
    http.requestId = static_cast<int>(ReqId::ID_LOGIN_UESR);
    http.server = server;
    coordinator.reduce(flow.flowId, http);

    AuthOutcome connected;
    connected.kind = AuthOutcomeKind::TcpConnected;
    coordinator.reduce(flow.flowId, connected);
    AuthOutcome loggedIn;
    loggedIn.kind = AuthOutcomeKind::ChatLoginSucceeded;
    QCOMPARE(coordinator.reduce(flow.flowId, loggedIn).kind,
             std::optional<AuthActionKind>(AuthActionKind::ShowChat));

    ClientSession session;
    session.beginSession();
    QSignalSpy resetSpy(&session, &ClientSession::sessionReset);
    AuthOutcome disconnected;
    disconnected.kind = AuthOutcomeKind::AbnormalDisconnect;
    const AuthAction action = coordinator.reduce(flow.flowId, disconnected);
    QCOMPARE(action.kind, std::optional<AuthActionKind>(AuthActionKind::ShowLogin));

    QVERIFY(session.resetSession(SessionResetReason::UnexpectedDisconnect));
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(qvariant_cast<SessionResetReason>(resetSpy.at(0).at(0)),
             SessionResetReason::UnexpectedDisconnect);
}

QTEST_MAIN(AuthFlowComponentTests)

#include "auth_flow_component_tests.moc"
