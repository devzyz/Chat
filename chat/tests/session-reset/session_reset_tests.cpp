#include "clientsession.h"
#include "global.h"
#include "tcpmgr.h"
#include "userdata.h"
#include "usermgr.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>
#include <spdlog/sinks/null_sink.h>

class SessionResetTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void accountStateDoesNotCrossLoginSessions();
    void resetDestroysOwnedSessionUiOnceAndPreservesReason();
    void resetDropsPendingTextBatchBeforeAnOldFailureArrives();
};

void SessionResetTests::initTestCase()
{
    qRegisterMetaType<SessionResetReason>("SessionResetReason");
    spdlog::set_default_logger(
        spdlog::create<spdlog::sinks::null_sink_mt>("session-reset-tests"));
}

void SessionResetTests::cleanupTestCase()
{
    TcpMgr::ReleaseInstance();
    UserMgr::ReleaseInstance();
}

void SessionResetTests::accountStateDoesNotCrossLoginSessions()
{
    ClientSession session;
    auto userMgr = UserMgr::GetInstance();
    session.beginSession();

    const QString preservedGateEndpoint = QStringLiteral("http://127.0.0.1:18080");
    gate_url_prefix = preservedGateEndpoint;
    userMgr->SetToken(QStringLiteral("synthetic-session-token"));
    userMgr->SetInfo(std::make_shared<UserInfo>(101, QStringLiteral("account-a"),
                                                QStringLiteral("description"),
                                                QStringLiteral("avatar"), 1));

    QJsonObject apply;
    apply["fromuid"] = 201;
    apply["applyname"] = QStringLiteral("applicant-a");
    apply["applydescription"] = QStringLiteral("description");
    apply["applyicon"] = QStringLiteral("avatar");
    apply["applysex"] = 0;
    apply["status"] = 0;
    apply["touid"] = 101;
    apply["description"] = QStringLiteral("request");
    apply["backname"] = QStringLiteral("alias");
    userMgr->AddApplyList(QJsonArray{apply});

    QJsonObject friendObject;
    friendObject["uid"] = 301;
    friendObject["name"] = QStringLiteral("friend-a");
    friendObject["description"] = QStringLiteral("description");
    friendObject["icon"] = QStringLiteral("avatar");
    friendObject["sex"] = 0;
    friendObject["backname"] = QStringLiteral("alias");
    userMgr->AddFriendList(QJsonArray{friendObject});
    userMgr->AddChatInfo(401, std::make_shared<ChatInfo>(301, 401, 777));
    userMgr->SetUidToChatId(301, 401);
    userMgr->SetCurrentChatId(401);
    userMgr->SetIsLoadFinish(true);
    userMgr->UpdateContactLoadedCount();

    QCOMPARE(userMgr->GetToken(), QStringLiteral("synthetic-session-token"));
    QVERIFY(userMgr->GetUserInfo());
    QVERIFY(userMgr->AlreadyApplyAddFriend(201));
    QVERIFY(userMgr->CheckIsFriendById(301));
    QVERIFY(userMgr->GetChatInfo(401));
    QCOMPARE(userMgr->GetUidToChatId(301), 401);

    QSignalSpy resetSpy(&session, &ClientSession::sessionReset);
    QVERIFY(session.resetSession(SessionResetReason::Logout));
    QCOMPARE(resetSpy.count(), 1);

    QVERIFY(!userMgr->GetUserInfo());
    QCOMPARE(userMgr->GetUid(), 0);
    QVERIFY(userMgr->GetToken().isEmpty());
    QVERIFY(!userMgr->AlreadyApplyAddFriend(201));
    QVERIFY(!userMgr->CheckIsFriendById(301));
    QVERIFY(!userMgr->GetChatInfo(401));
    QCOMPARE(userMgr->GetUidToChatId(301), -1);
    QCOMPARE(userMgr->GetCurrentLoadChatId(), 0);
    QVERIFY(!userMgr->ChatIsLoadFinish());
    QVERIFY(userMgr->GetSomeContactList().empty());
    QCOMPARE(gate_url_prefix, preservedGateEndpoint);
}

void SessionResetTests::resetDestroysOwnedSessionUiOnceAndPreservesReason()
{
    ClientSession session;
    auto *ownedSessionUi = new QObject;
    QPointer<QObject> guardedUi = ownedSessionUi;
    QSignalSpy destroyedSpy(ownedSessionUi, &QObject::destroyed);
    QSignalSpy resetSpy(&session, &ClientSession::sessionReset);

    session.beginSession(ownedSessionUi);
    QVERIFY(session.isActive());
    QVERIFY(session.resetSession(SessionResetReason::Kicked));
    QVERIFY(!session.isActive());
    QCOMPARE(destroyedSpy.count(), 1);
    QVERIFY(guardedUi.isNull());
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(qvariant_cast<SessionResetReason>(resetSpy.at(0).at(0)),
             SessionResetReason::Kicked);

    QVERIFY(!session.resetSession(SessionResetReason::UnexpectedDisconnect));
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(destroyedSpy.count(), 1);
}

void SessionResetTests::resetDropsPendingTextBatchBeforeAnOldFailureArrives()
{
    ClientSession session;
    auto tcpMgr = TcpMgr::GetInstance();
    session.beginSession();

    QJsonObject message;
    message["msg_uuid"] = QStringLiteral("old-client-message");
    message["msg_content"] = QStringLiteral("synthetic message");
    QJsonObject request;
    request["chat_id"] = 501;
    request["text_array"] = QJsonArray{message};
    emit tcpMgr->sig_send_data(
        ReqId::ID_TEXT_CHAT_MSG_REQ,
        QJsonDocument(request).toJson(QJsonDocument::Compact));

    QVERIFY(session.resetSession(SessionResetReason::SwitchAccount));

    message["msg_uuid"] = QStringLiteral("post-reset-message");
    request["text_array"] = QJsonArray{message};
    emit tcpMgr->sig_send_data(
        ReqId::ID_TEXT_CHAT_MSG_REQ,
        QJsonDocument(request).toJson(QJsonDocument::Compact));

    int failureCount = 0;
    QVector<QString> failedClientIds;
    const QMetaObject::Connection failureConnection = connect(
        tcpMgr.get(), &TcpMgr::sig_text_chat_msg_failed, this,
        [&](int, const QVector<QString> &clientIds) {
            ++failureCount;
            failedClientIds = clientIds;
        });
    QJsonObject oldFailure;
    oldFailure["error"] = 1;
    oldFailure["chat_id"] = 501;
    const QByteArray oldFailureBytes =
        QJsonDocument(oldFailure).toJson(QJsonDocument::Compact);
    tcpMgr->handleMsg(ReqId::ID_TEXT_CHAT_MSG_RSP,
                      oldFailureBytes.size(), oldFailureBytes);

    QCOMPARE(failureCount, 1);
    QVERIFY(failedClientIds.isEmpty());
    disconnect(failureConnection);
}

QTEST_MAIN(SessionResetTests)

#include "session_reset_tests.moc"
