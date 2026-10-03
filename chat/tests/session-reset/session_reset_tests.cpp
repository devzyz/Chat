#include "clientsession.h"
#include "messageservice.h"
#include <QTemporaryDir>
#include <QScopeGuard>
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

/** @brief 验证账号隔离、会话清理及持久化消息重试边界。 */
class SessionResetTests : public QObject
{
    Q_OBJECT

private slots:
    /** 注册重置原因元类型并安装静默测试日志器。 */
    void initTestCase();
    /** 释放测试使用的 TCP 和用户管理单例。 */
    void cleanupTestCase();
    /** @brief 验证退出后清空账号数据并保留应用配置。 */
    void accountStateDoesNotCrossLoginSessions();
    /** 验证重置只销毁一次所属会话界面并保留重置原因。 */
    void resetDestroysOwnedSessionUiOnceAndPreservesReason();
    /** 验证停止会话后忽略旧批次失败响应。 */
    void resetDropsPendingTextBatchBeforeAnOldFailureArrives();
    /** 验证未确认批次断连重开后恢复，ACK 仅匹配精确 UUID。 */
    void uncertainBatchSurvivesDisconnectAndMatchesExactUuid();
    /** 验证切换账号后旧发送结果和重试不会污染当前账号。 */
    void retryDoesNotCrossAuthenticatedAccounts();
    /** @brief 验证重连认证后沿用原 UUID 和业务载荷，仅递增发送尝试。 */
    void reconnectResendsIdenticalWirePayloadAfterAuthentication();
    /** @brief 退出只有收到撤销确认才清理账号，失败仍可重试。 */
    void logoutWaitsForRevocationAndPreservesStateOnFailure();
    /** @brief 验证社交读取落盘、回包关联、超时与账号生命周期隔离。 */
    void socialRequestsRespectSessionLifecycle();
};

void SessionResetTests::initTestCase()
{
    qRegisterMetaType<SessionResetReason>("SessionResetReason");
    spdlog::set_default_logger(
        spdlog::create<spdlog::sinks::null_sink_mt>("session-reset-tests"));
}

void SessionResetTests::cleanupTestCase()
{
    TcpMgr::releaseInstance();
    UserMgr::releaseInstance();
}

void SessionResetTests::accountStateDoesNotCrossLoginSessions()
{
    ClientSession session;
    auto userMgr = UserMgr::instance();
    session.beginSession();

    const QString preservedGateEndpoint = QStringLiteral("http://127.0.0.1:18080");
    gate_url_prefix = preservedGateEndpoint;
    userMgr->setToken(QStringLiteral("synthetic-session-token"));
    userMgr->setUserInfo(std::make_shared<UserInfo>(101, QStringLiteral("account-a"),
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
    userMgr->addFriendApplications(QJsonArray{apply});

    QJsonObject friendObject;
    friendObject["uid"] = 301;
    friendObject["name"] = QStringLiteral("friend-a");
    friendObject["description"] = QStringLiteral("description");
    friendObject["icon"] = QStringLiteral("avatar");
    friendObject["sex"] = 0;
    friendObject["backname"] = QStringLiteral("alias");
    userMgr->addFriends(QJsonArray{friendObject});
    userMgr->addChatInfo(401, std::make_shared<ChatInfo>(301, 401, 777));
    userMgr->addPrivateChatMapping(301, 401);
    userMgr->setChatListCursor(401);
    userMgr->setChatListFullyLoaded(true);
    userMgr->advanceContactPage();

    QCOMPARE(userMgr->token(), QStringLiteral("synthetic-session-token"));
    QVERIFY(userMgr->userInfo());
    QVERIFY(userMgr->hasFriendApplication(201));
    QVERIFY(userMgr->isFriend(301));
    QVERIFY(userMgr->chatInfo(401));
    QCOMPARE(userMgr->privateChatIdFor(301), 401);

    QSignalSpy resetSpy(&session, &ClientSession::sessionReset);
    QVERIFY(session.resetSession(SessionResetReason::Logout));
    QCOMPARE(resetSpy.count(), 1);

    QVERIFY(!userMgr->userInfo());
    QCOMPARE(userMgr->uid(), 0);
    QVERIFY(userMgr->token().isEmpty());
    QVERIFY(!userMgr->hasFriendApplication(201));
    QVERIFY(!userMgr->isFriend(301));
    QVERIFY(!userMgr->chatInfo(401));
    QCOMPARE(userMgr->privateChatIdFor(301), -1);
    QCOMPARE(userMgr->chatListCursor(), 0);
    QVERIFY(!userMgr->isChatListFullyLoaded());
    QVERIFY(userMgr->nextContactPage().empty());
    QCOMPARE(gate_url_prefix, preservedGateEndpoint);

    QTemporaryDir directory;
    userMgr->setUserInfo(std::make_shared<UserInfo>(101, "self", "", "", 0));
    auto *messages = userMgr->messages();
    QSignalSpy restored(messages, &MessageService::directoryRestored);
    QSignalSpy changed(messages, &MessageService::directoryChanged);
    messages->start(directory.path(), 101);
    QTRY_COMPARE(restored.size(), 1);
    const QJsonObject application{{"error", 0}, {"fromuid", 301}, {"touid", 101}, {"applyname", "peer"}};
    const auto notification = QJsonDocument(application).toJson(QJsonDocument::Compact);
    TcpMgr::instance()->handleMessage(ID_NOTIFY_ADD_FRIEND_REQ, notification.size(), notification);
    QTRY_COMPARE(changed.size(), 1);
    QVERIFY(userMgr->hasFriendApplication(301));
    const QJsonObject approval{{"error", 0}, {"chatid", 401},
        {"applyinfo", QJsonObject{{"applyuid", 301}, {"applyname", "peer"}}},
        {"authinfo", QJsonObject{{"backname", "alias"}}}};
    const auto response = QJsonDocument(approval).toJson(QJsonDocument::Compact);
    TcpMgr::instance()->handleMessage(ID_AUTH_FRIEND_RSP, response.size(), response);
    QTRY_COMPARE(changed.size(), 2);
    QVERIFY(userMgr->isFriend(301));
    QCOMPARE(userMgr->privateChatIdFor(301), 401);
    QCOMPARE(userMgr->nextContactPage().size(), size_t(1));
    // 重复通知不重复联系人，退出清空内存后仍能从原账号数据库恢复。
    TcpMgr::instance()->handleMessage(ID_AUTH_FRIEND_RSP, response.size(), response);
    QTRY_COMPARE(changed.size(), 3);
    QCOMPARE(userMgr->nextContactPage().size(), size_t(1));
    const QJsonObject groupPage{{"error", 0}, {"current_chat_id", 402}, {"load_more", false},
        {"chat_list", QJsonArray{QJsonObject{{"chat_id", 402}, {"type", "group"}, {"group_name", "Friends"}}}}};
    const auto groupBytes = QJsonDocument(groupPage).toJson(QJsonDocument::Compact);
    TcpMgr::instance()->handleMessage(ID_LOAD_CHAT_LIST_RSP, groupBytes.size(), groupBytes);
    QTRY_COMPARE(changed.size(), 4);
    QVERIFY(userMgr->chatInfo(402));
    QCOMPARE(userMgr->chatInfo(402)->getChatType(), ChatType::GROUP);
    QCOMPARE(userMgr->chatInfo(402)->name(), QString("Friends"));
    QCOMPARE(userMgr->privateChatIdFor(0), -1);
    userMgr->resetSession();
    messages->start(directory.path(), 101);
    QTRY_COMPARE(restored.size(), 2);
    QVERIFY(userMgr->isFriend(301));
    QCOMPARE(userMgr->privateChatIdFor(301), 401);
    std::vector<std::shared_ptr<ApplyInfo>> applications;
    QVERIFY(userMgr->chatInfo(402));
    QCOMPARE(userMgr->chatInfo(402)->getChatType(), ChatType::GROUP);
    userMgr->appendFriendApplicationsTo(applications);
    QCOMPARE(applications.size(), size_t(1));
    QCOMPARE(applications.front()->_status, 1);
    userMgr->resetSession();
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

/** 生成指定发送者与 UUID 的固定文本请求。 */
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
    const auto ack = /** 构造精确关联 UUID 与服务端消息编号的成功 ACK。 */ [](QString uuid, int id) { return QJsonObject{{"error", 0}, {"chat_id", 501},
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
    auto tcp = TcpMgr::instance();
    tcp->resetConnection(true);
    QTcpServer peer;
    QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
    ServerInfo endpoint;
    endpoint.Host = "127.0.0.1";
    endpoint.Port = QString::number(peer.serverPort());
    QSignalSpy connected(tcp.get(), &TcpMgr::connectionAttemptFinished);
    QSignalSpy closed(tcp.get(), &TcpMgr::connectionClosed);
    tcp->connectToServer(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(connected.size(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingConnections(), 2000);
    std::unique_ptr<QTcpSocket> first(peer.nextPendingConnection());
    const QByteArray login = QJsonDocument(QJsonObject{{"error", 0}, {"uid", 101}, {"token", "synthetic-resume-fixture"}}).toJson();
    tcp->handleMessage(ReqId::ID_CHAT_LOGIN_RSP, login.size(), login);
    ClientSession session;
    QPointer<QObject> draft = new QObject(&session); draft->setProperty("draft", "unsent draft");
    session.beginSession(draft);
    const auto cleanup = qScopeGuard(/** @brief 失败路径也停止重连和账号存储。 */ [&] { session.resetSession(SessionResetReason::UnexpectedDisconnect); });
    const QByteArray request = QJsonDocument(QJsonObject{{"from_uid", 101}, {"to_uid", 102},
        {"chat_id", 501}, {"text_array", QJsonArray{QJsonObject{
        {"msg_uuid", "00000000-0000-4000-8000-000000000004"}, {"msg_content", "retry body"}}}}})
        .toJson(QJsonDocument::Compact);
    auto *service = UserMgr::instance()->messages();
    service->start(directory.path(), 101);
    service->send(QJsonDocument::fromJson(request).object());
    QTRY_VERIFY_WITH_TIMEOUT(first->bytesAvailable() > 4, 2000);
    const QByteArray originalWire = first->readAll();
    first->abort();
    QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty(), 2000);
    QVERIFY(!closed.last()[0].toBool());
    QTRY_COMPARE_WITH_TIMEOUT(connected.size(), 2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingConnections(), 2000);
    std::unique_ptr<QTcpSocket> second(peer.nextPendingConnection());
    QTRY_VERIFY_WITH_TIMEOUT(second->bytesAvailable() > 4, 2000);
    const auto authentication = QJsonDocument::fromJson(second->readAll().mid(4)).object();
    QCOMPARE(authentication["uid"].toInt(), 101);
    QCOMPARE(authentication["token"].toString(), QString("synthetic-resume-fixture"));
    QVERIFY(draft); QCOMPARE(draft->property("draft").toString(), QString("unsent draft"));
    tcp->handleMessage(ReqId::ID_CHAT_LOGIN_RSP, login.size(), login);
    QVERIFY(!session.isReconnecting());
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

void SessionResetTests::socialRequestsRespectSessionLifecycle()
{
    QTemporaryDir directory;
    auto tcp = TcpMgr::instance();
    auto user = UserMgr::instance();
    const auto oldGate = gate_url_prefix;
    const auto cleanup = qScopeGuard(/** @brief 失败路径也清理本次账号状态并恢复应用配置。 */ [&] {
        tcp->resetConnection(true); user->resetSession(); gate_url_prefix = oldGate;
    });
    gate_url_prefix.clear(); tcp->resetConnection(true); user->resetSession();
    QSignalSpy requests(tcp.get(), &TcpMgr::sendRequested);
    const auto deliver = /** @brief 通过生产业务回包入口注入合成服务响应。 */ [&](ReqId id, QJsonObject row) {
        const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact);
        tcp->handleMessage(id, bytes.size(), bytes);
    };
    const auto latest = /** @brief 读取最近发送的社交请求，跳过消息同步等其他事件。 */ [&] {
        for (auto it = requests.crbegin(); it != requests.crend(); ++it)
            if ((*it)[0].value<ReqId>() == ID_SOCIAL_DIRECTORY_REQ)
                return QJsonDocument::fromJson((*it)[1].toByteArray()).object();
        return QJsonObject{};
    };
    const auto login = /** @brief 协商生产社交能力并将存储限定到临时账号目录。 */ [&](int uid) {
        tcp->beginSession();
        deliver(ID_CHAT_LOGIN_RSP, {{"error",0},{"uid",uid},{"capabilities",QJsonArray{"basic_social_v1"}}});
        user->messages()->start(directory.path() + "/" + QString::number(uid), uid, false, true);
    };
    login(101); QVERIFY(tcp->supportsSocial());
    auto *service = user->messages();
    QSignalSpy changed(service, &MessageService::directoryChanged);
    auto profile = latest(); QCOMPARE(profile["kind"].toString(), QString("profile"));
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",profile["request_id"]},{"error",0},
        {"profile",QJsonObject{{"id",101},{"uid",101},{"name","owner"},{"profile_revision","1"}}}});
    QTRY_COMPARE(latest()["kind"].toString(), QString("contacts"));
    QVERIFY(!service->socialReady());
    auto contacts = latest();
    const QJsonObject peer{{"id",102},{"uid",102},{"name",QString(255,QChar(0x540d))},
        {"description",QString(255,QChar(0x8ff0))},{"backname",QString(255,QChar(0x5907))},
        {"chat_id",501},{"profile_revision","1"},{"relationship_active",true},{"relationship_revision","1"}};
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",contacts["request_id"]},{"error",0},
        {"items",QJsonArray{peer}},{"load_more",true},{"next","102"}});
    QTRY_COMPARE(latest()["after"].toString(), QString("102"));
    QVERIFY(!service->socialReady());
    contacts = latest();
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",contacts["request_id"]},{"error",0},
        {"items",QJsonArray{}},{"load_more",false}});
    QTRY_COMPARE(latest()["kind"].toString(), QString("applications"));
    QVERIFY(service->socialReady());
    QCOMPARE(user->friendById(102)->_name, peer["name"].toString());
    auto applications = latest();
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",applications["request_id"]},{"error",1},{"social_error","StorageUnavailable"}});
    QVERIFY(service->socialReady()); // An application-list failure must not block authorized chats.

    QObject receiver;
    int completed = 0;
    QJsonObject terminal;
    const auto accept = /** @brief 记录当前接收者看到的唯一终态。 */ [&](QJsonObject response) {
        ++completed; terminal = response;
    };
    tcp->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"}}, &receiver, accept);
    const auto request = latest();
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id","unknown"},{"error",0}});
    deliver(ID_PROFILE_UPDATE_RSP, {{"request_id",request["request_id"]},{"error",0}});
    QCOMPARE(completed,0);
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",request["request_id"]},{"error",0}});
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",request["request_id"]},{"error",0}});
    QCOMPARE(completed,1);
    int destroyedCallbacks = 0;
    auto removed = std::make_unique<QObject>();
    tcp->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"}}, removed.get(),
        /** @brief 已销毁接收者不得收到完成或超时。 */ [&](QJsonObject) { ++destroyedCallbacks; });
    const auto abandoned = latest(); removed.reset();
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",abandoned["request_id"]},{"error",0}});
    QCOMPARE(destroyedCallbacks,0);
    tcp->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"}}, &receiver, accept);
    const auto timedOut = latest();
    QTRY_COMPARE_WITH_TIMEOUT(completed,2,15000);
    QCOMPARE(terminal["social_error"].toString(),QString("Timeout"));
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",timedOut["request_id"]},{"error",0}});
    QCOMPARE(completed,2);
    tcp->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"}}, &receiver, accept);
    const auto old = latest();
    tcp->resetConnection(true); tcp->resetConnection(true); user->resetSession();
    login(202);
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",old["request_id"]},{"error",0}});
    QCOMPARE(completed,2);
    tcp->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"}}, &receiver, accept);
    const auto fresh = latest();
    deliver(ID_SOCIAL_DIRECTORY_RSP, {{"request_id",fresh["request_id"]},{"error",0}});
    QCOMPARE(completed,3); QCOMPARE(destroyedCallbacks,0);
    QSignalSpy legacyCompletion(tcp.get(), &TcpMgr::requestCompleted);
    for (const int error : {0, 1001}) {
        deliver(ID_AUTH_FRIEND_RSP, {{"error", error}});
        QCOMPARE(legacyCompletion.size(), error == 0 ? 1 : 2);
        QCOMPARE(legacyCompletion.last()[0].value<ReqId>(), ID_AUTH_FRIEND_RSP);
        QCOMPARE(legacyCompletion.last()[1].toInt(), error);
    }
    tcp->resetConnection(true);
    QTRY_VERIFY(!QFileInfo::exists(directory.path() + "/202/messages.lock"));
}

void SessionResetTests::logoutWaitsForRevocationAndPreservesStateOnFailure()
{
    const auto previousGate = gate_url_prefix;
    ClientSession session;
    auto user = UserMgr::instance();
    const auto cleanup = qScopeGuard(/** @brief 结束本测试会话并恢复应用服务地址。 */ [&] {
        session.resetSession(SessionResetReason::UnexpectedDisconnect); gate_url_prefix = previousGate;
    });
    QTcpServer gate; QVERIFY(gate.listen(QHostAddress::LocalHost, 0));
    gate_url_prefix = "http://127.0.0.1:" + QString::number(gate.serverPort());
    user->setUserInfo(std::make_shared<UserInfo>(101, "fixture", ""));
    user->setToken("synthetic-logout-fixture");
    QPointer<QObject> owned = new QObject(&session); session.beginSession(owned);
    QSignalSpy completed(&session, &ClientSession::logoutFinished);
    for (int attempt = 0; attempt < 2; ++attempt) {
        QVERIFY(session.requestLogout(SessionResetReason::Logout));
        QVERIFY(session.isActive()); QVERIFY(owned);
        QTRY_VERIFY_WITH_TIMEOUT(gate.hasPendingConnections(), 2000);
        std::unique_ptr<QTcpSocket> socket(gate.nextPendingConnection());
        QByteArray bytes;
        QTRY_VERIFY_WITH_TIMEOUT((bytes += socket->readAll()).contains("synthetic-logout-fixture"), 2000);
        QVERIFY(bytes.startsWith("POST /logout HTTP/1.1"));
        const auto payload = QJsonDocument::fromJson(bytes.mid(bytes.indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(payload.size(), 2); QCOMPARE(payload["uid"].toInt(), 101);
        const QByteArray body = attempt == 0 ? "{\"error\":1002}" : "{\"error\":0}";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), attempt + 1, 2000);
        QCOMPARE(completed.last()[0].toBool(), attempt == 1);
        QCOMPARE(session.isActive(), attempt == 0);
        if (attempt == 0) { QVERIFY(owned); QCOMPARE(user->uid(), 101); }
    }
    QVERIFY(!owned); QVERIFY(user->token().isEmpty());
}

QTEST_MAIN(SessionResetTests)

#include "session_reset_tests.moc"
