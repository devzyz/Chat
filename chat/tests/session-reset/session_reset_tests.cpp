#include "clientsession.h"
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

void SessionResetTests::uncertainBatchSurvivesDisconnectAndMatchesExactUuid()
{
    auto tcp = TcpMgr::GetInstance();
    tcp->resetConnection(true);
    UserMgr::GetInstance()->SetInfo(std::make_shared<UserInfo>(101, "a", "", "", 0));
    tcp->beginSession();
    auto send = [&](const QString &uuid) {
        QJsonObject request{{"from_uid", 101}, {"to_uid", 102}, {"chat_id", 501},
            {"text_array", QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"msg_content", "body"}}}}};
        emit tcp->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, QJsonDocument(request).toJson());
    };
    const QString first = "00000000-0000-4000-8000-000000000001";
    const QString second = "00000000-0000-4000-8000-000000000002";
    send(first);
    send(second);
    tcp->resetConnection(false);
    tcp->beginSession();
    QSignalSpy failures(tcp.get(), &TcpMgr::sig_text_chat_msg_failed);
    auto fail = [&](const QString &uuid) {
        const QByteArray response = QJsonDocument(QJsonObject{{"error", 1011}, {"chat_id", 501},
            {"commit_error", "Conflict"}, {"client_msg_uuids", QJsonArray{uuid}}}).toJson();
        tcp->handleMsg(ReqId::ID_TEXT_CHAT_MSG_RSP, response.size(), response);
    };
    fail(second);
    QCOMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<QVector<QString>>(failures[0][1]), QVector<QString>{second});
    fail(first);
    QCOMPARE(qvariant_cast<QVector<QString>>(failures[1][1]), QVector<QString>{first});
    fail(first);
    QVERIFY(qvariant_cast<QVector<QString>>(failures[2][1]).isEmpty());
    send(first);
    const QByteArray unavailable = QJsonDocument(QJsonObject{{"error", 1011}, {"chat_id", 501},
        {"commit_error", "StorageUnavailable"}, {"client_msg_uuids", QJsonArray{first}}}).toJson();
    tcp->handleMsg(ReqId::ID_TEXT_CHAT_MSG_RSP, unavailable.size(), unavailable);
    const QByteArray malformedAck = QJsonDocument(QJsonObject{{"error", 0}, {"chat_id", 501},
        {"client_msg_uuids", QJsonArray{first}}}).toJson();
    tcp->handleMsg(ReqId::ID_TEXT_CHAT_MSG_RSP, malformedAck.size(), malformedAck);
    fail(first);
    QCOMPARE(qvariant_cast<QVector<QString>>(failures.last()[1]), QVector<QString>{first});
    send(first);
    const QByteArray ack = QJsonDocument(QJsonObject{{"error", 0}, {"chat_id", 501},
        {"client_msg_uuids", QJsonArray{first}}, {"uuid_msgId", QJsonArray{
            QJsonObject{{"msg_uuid", first}, {"message_id", 9001}}}}}).toJson();
    tcp->handleMsg(ReqId::ID_TEXT_CHAT_MSG_RSP, ack.size(), ack);
    fail(first);
    QVERIFY(qvariant_cast<QVector<QString>>(failures.last()[1]).isEmpty());
    tcp->resetConnection(true);
}

void SessionResetTests::retryDoesNotCrossAuthenticatedAccounts()
{
    auto tcp = TcpMgr::GetInstance();
    tcp->resetConnection(true);
    UserMgr::GetInstance()->SetInfo(std::make_shared<UserInfo>(101, "a", "", "", 0));
    tcp->beginSession();
    const QString uuid = "00000000-0000-4000-8000-000000000003";
    const QByteArray request = QJsonDocument(QJsonObject{{"from_uid", 101}, {"to_uid", 102},
        {"chat_id", 501}, {"text_array", QJsonArray{QJsonObject{{"msg_uuid", uuid},
        {"msg_content", "body"}}}}}).toJson();
    emit tcp->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, request);
    tcp->resetConnection(false);
    const QByteArray login = QJsonDocument(QJsonObject{{"error", 0}, {"uid", 202}}).toJson();
    tcp->handleMsg(ReqId::ID_CHAT_LOGIN_RSP, login.size(), login);
    QSignalSpy failures(tcp.get(), &TcpMgr::sig_text_chat_msg_failed);
    const QByteArray response = QJsonDocument(QJsonObject{{"error", 1011}, {"chat_id", 501},
        {"commit_error", "Conflict"}, {"client_msg_uuids", QJsonArray{uuid}}}).toJson();
    tcp->handleMsg(ReqId::ID_TEXT_CHAT_MSG_RSP, response.size(), response);
    QVERIFY(qvariant_cast<QVector<QString>>(failures[0][1]).isEmpty());
    tcp->resetConnection(true);
}

void SessionResetTests::reconnectResendsIdenticalWirePayloadAfterAuthentication()
{
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
    emit tcp->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, request);
    QTRY_COMPARE_WITH_TIMEOUT(first->bytesAvailable(), request.size() + 4, 2000);
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
    QTRY_COMPARE_WITH_TIMEOUT(second->bytesAvailable(), originalWire.size(), 2000);
    QCOMPARE(second->readAll(), originalWire);
    tcp->resetConnection(true);
}

QTEST_MAIN(SessionResetTests)

#include "session_reset_tests.moc"
