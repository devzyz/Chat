#include "clientsession.h"
#include "messageservice.h"
#include <QTemporaryDir>
#include "global.h"
#include "tcpmgr.h"
#include "userdata.h"
#include "usermgr.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
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
    void uncertainBatchSurvivesDisconnectAndMatchesExactUuid();
    void retryDoesNotCrossAuthenticatedAccounts();
    void reconnectResendsIdenticalWirePayloadAfterAuthentication();
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

static QJsonObject outgoing(const QString &uuid, int uid = 101)
{
    return {{"from_uid", uid}, {"to_uid", 102}, {"chat_id", 501},
        {"text_array", QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"msg_content", "body"}}}}};
}

void SessionResetTests::resetDropsPendingTextBatchBeforeAnOldFailureArrives()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy sent(&service, &MessageService::sendRequested);
    QSignalSpy changed(&service, &MessageService::messagesChanged);
    service.start(directory.path(), 101);
    service.send(outgoing("old-client-message"));
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 1, 2000);
    service.pauseOutgoing();
    service.stop();
    const auto changes = changed.size();
    service.acceptSendResponse({{"error", 1}, {"chat_id", 501}, {"commit_error", "Conflict"},
        {"attempt_id", "1"}, {"client_msg_uuids", QJsonArray{"old-client-message"}}});
    service.send(outgoing("after-reset"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(changed.size(), changes);
    service.start(directory.path(), 101);
    QSignalSpy loaded(&service, &MessageService::historyLoaded);
    service.loadHistory(501);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 2000);
    QCOMPARE(qvariant_cast<QVector<StoredMessage>>(loaded.first()[2]).size(), 1);
    QCOMPARE(sent.size(), 1); // Explicit logout pauses the durable intent.
}

void SessionResetTests::uncertainBatchSurvivesDisconnectAndMatchesExactUuid()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 101);
    store.saveOutgoingRequest(outgoing("first"));
    store.saveOutgoingRequest(outgoing("second"));
    QCOMPARE(store.dispatchDue(0).size(), 2);
    store.close();
    store.open(directory.path(), 101);
    store.resumeOutgoing();
    QCOMPARE(store.dispatchDue(1).size(), 2);
    const auto ack = [](QString uuid, int id) { return QJsonObject{{"error", 0}, {"chat_id", 501},
        {"from_uid", 101}, {"to_uid", 102}, {"client_msg_uuids", QJsonArray{uuid}},
        {"uuid_msgId", QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"message_id", id}}}}}; };
    store.acceptSendResponse(ack("second", 902));
    QVERIFY_EXCEPTION_THROWN(store.acceptSendResponse({{"error", 0}, {"chat_id", 501}, {"from_uid", 101},
        {"client_msg_uuids", QJsonArray{"first"}}}), std::exception);
    store.acceptSendResponse({{"error", 1}, {"chat_id", 501}, {"attempt_id", "2"},
        {"client_msg_uuids", QJsonArray{"first"}}, {"commit_error", "StorageUnavailable"}});
    store.acceptSendResponse(ack("first", 901));
    store.acceptSendResponse(ack("first", 901));
    const auto rows = store.history(501, 0, 50).messages;
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.front().messageId, 901);
    QCOMPARE(rows.back().messageId, 902);
    QVERIFY(store.dispatchDue(100000).isEmpty());
}

void SessionResetTests::retryDoesNotCrossAuthenticatedAccounts()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy sent(&service, &MessageService::sendRequested);
    service.start(directory.path() + "/first", 101);
    service.send(outgoing("account-101"));
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 1, 2000);
    service.start(directory.path() + "/second", 202);
    service.acceptSendResponse({{"error", 0}, {"chat_id", 501}, {"from_uid", 101}, {"to_uid", 102},
        {"uuid_msgId", QJsonArray{QJsonObject{{"msg_uuid", "account-101"}, {"message_id", 901}}}}});
    QSignalSpy loaded(&service, &MessageService::historyLoaded);
    service.loadHistory(501);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 2000);
    QVERIFY(qvariant_cast<QVector<StoredMessage>>(loaded.first()[2]).isEmpty());
    QCOMPARE(sent.size(), 1);
}

void SessionResetTests::reconnectResendsIdenticalWirePayloadAfterAuthentication()
{
    QTemporaryDir directory;
    gate_url_prefix.clear();
    auto tcp = TcpMgr::GetInstance();
    tcp->resetConnection(true);
    QTcpServer peer;
    QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
    ServerInfo endpoint;
    endpoint.Host = "127.0.0.1";
    endpoint.Port = QString::number(peer.serverPort());
    QSignalSpy connected(tcp.get(), &TcpMgr::sig_tcp_connect_success);
    QSignalSpy closed(tcp.get(), &TcpMgr::sig_connection_close);
    tcp->slot_tcp_connect(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(connected.size(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingConnections(), 2000);
    std::unique_ptr<QTcpSocket> first(peer.nextPendingConnection());
    const QByteArray login = QJsonDocument(QJsonObject{{"error", 0}, {"uid", 101}}).toJson();
    tcp->handleMsg(ReqId::ID_CHAT_LOGIN_RSP, login.size(), login);
    const QByteArray request = QJsonDocument(QJsonObject{{"from_uid", 101}, {"to_uid", 102},
        {"chat_id", 501}, {"text_array", QJsonArray{QJsonObject{
        {"msg_uuid", "00000000-0000-4000-8000-000000000004"}, {"msg_content", "retry body"}}}}})
        .toJson(QJsonDocument::Compact);
    auto *service = UserMgr::GetInstance()->messages();
    service->start(directory.path(), 101);
    service->send(QJsonDocument::fromJson(request).object());
    QTRY_VERIFY_WITH_TIMEOUT(first->bytesAvailable() > 4, 2000);
    const QByteArray originalWire = first->readAll();
    first->abort();
    QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty(), 2000);
    QVERIFY(!closed.last()[0].toBool());
    tcp->slot_tcp_connect(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(connected.size(), 2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingConnections(), 2000);
    std::unique_ptr<QTcpSocket> second(peer.nextPendingConnection());
    QCOMPARE(second->bytesAvailable(), 0);
    tcp->handleMsg(ReqId::ID_CHAT_LOGIN_RSP, login.size(), login);
    QSignalSpy recovery(service, &MessageService::syncRequested);
    service->start(directory.path(), 101);
    QTRY_COMPARE_WITH_TIMEOUT(recovery.size(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(second->bytesAvailable() > 4, 2000);
    auto verification = QJsonDocument::fromJson(second->readAll().mid(4)).object();
    QCOMPARE(verification["mode"].toString(), QString("sync_v1"));
    verification["error"] = 0;
    verification["msgs"] = QJsonArray{};
    verification["next_cursor"] = verification["after_id"];
    verification["load_more"] = false;
    service->acceptSyncPage(verification);
    QTRY_VERIFY_WITH_TIMEOUT(second->bytesAvailable() > 4, 2000);
    const auto replay = second->readAll();
    auto originalBody = QJsonDocument::fromJson(originalWire.mid(4)).object();
    auto replayBody = QJsonDocument::fromJson(replay.mid(4)).object();
    QCOMPARE(originalBody.take("attempt_id").toString(), QString("1"));
    QCOMPARE(replayBody.take("attempt_id").toString(), QString("2"));
    QCOMPARE(originalBody, replayBody);
    tcp->resetConnection(true);
}

QTEST_MAIN(SessionResetTests)

#include "session_reset_tests.moc"
