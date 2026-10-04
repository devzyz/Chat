#include "localmessagestore.h"
#include "messageservice.h"
#include "userstoragepaths.h"

#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QtTest>

/** 验证真实 SQLite 消息与回执持久化、重试身份、事务游标及账号隔离。 */
class MessageStorageTests : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 验证关系墓碑和版本拒绝旧重试，资料单调合并及摘要跨重启保持。 */
    void socialVersionsAndSummaries();
    /** @brief 验证私聊/群提醒去重、本人消息排除、查看边界、重启和账号隔离。 */
    void conversationAttentionPersistence();
    /** @brief 验证 schema 4 升级备份且不将无法判断的旧历史重新标为新消息。 */
    void schemaFourAttentionUpgrade();
    /** 验证离群、重新加入时旧请求与页被拒绝，旧历史和分页搜索保留。 */
    void groupEpochAndLocalSearch();
    /** 验证群目录、零收件人正文、重启和 outbox 保持幂等且不生成私聊回执。 */
    void groupMessagesPersistWithoutPrivateReceipts();
    /** 验证群同步不触发私聊回执，并在账号结束后停止轮询。 */
    void groupServiceSkipsReceipts();
    /** 验证目录事务、重启、本地三页分页及审批状态保持。 */
    void directoryPersistenceAndPagination();
    /** 验证目录异步恢复、失败不发布和切换账号隔离。 */
    void directoryServiceIsolation();
    /** 验证管理命令在并发目录刷新和重启后仍保存同一身份。 */
    void groupOperationSurvivesRefreshAndRestart();
    /** 验证 schema 2 升级保留消息并备份旧数据库。 */
    void schemaTwoDirectoryUpgrade();
    /** 验证重启恢复同步游标，迟到 ACK 不跳过尚未同步的消息区间。 */
    void restartAndIncrementalCursor();
    /** 验证待发消息、历史与 ACK 按完整身份合并并保留本地编号。 */
    void pendingHistoryAndAckMerge();
    /** 验证非法同步页整页回滚且游标不前进。 */
    void failedPageDoesNotAdvanceCursor();
    /** 验证本地历史分页顺序与服务端、账号存储隔离。 */
    void accountsAndLocalPagination();
    /** 验证消息服务同步流程并拒绝旧会话结果污染新账号。 */
    void serviceSyncAndSessionIsolation();
    /** @brief 验证分页补拉中已提交的正文不被后一页失败或空页阻止刷新。 */
    void committedSyncPageRemainsVisibleAfterFailure();
    /** 验证待发消息必须先持久化，存储失败不得发送。 */
    void outgoingRequiresDurableStorage();
    /** 验证增量刷新补全已展示区间，不截断为固定页长。 */
    void refreshKeepsTheDisplayedIntervalComplete();
    /** 验证回执可先于消息到达、重启后保留且等级单调，回执游标独立。 */
    void receiptsAreDurableMonotonicAndIndependent();
    /** 验证 Delivered 确认不能清除更新的 Read 上报意图。 */
    void deliveredAckCannotEraseNewReadIntent();
    /** 验证重试批次正文及 UUID 不变，attempt 递增且期限、预算有效。 */
    void outgoingBatchRetriesUseStableIdentityAndAttempt();
    /** 验证非法回执页回滚所有状态且不推进 revision 游标。 */
    void invalidReceiptPageRollsBackAndDoesNotAdvance();
    /** 验证消息服务回执上报往返及账号切换后的结果隔离。 */
    void serviceReceiptRoundTripAndAccountIsolation();
    /** 验证 schema 1 升级保留历史并创建原库备份。 */
    void schemaOneUpgradePreservesHistoryAndBackup();
    /** 验证资源发送意图在重启与重试预算耗尽后仍保留原资源身份。 */
    void resourceIntentSurvivesRecoveryAndRetryBudget();
    /** @brief 上传描述符在私聊和群重启后可合并规范历史，业务冲突仍整页回滚。 */
    void uploadedResourceCanonicalHistory();
};

void MessageStorageTests::conversationAttentionPersistence()
{
    QTemporaryDir root, otherRoot;
    LocalMessageStore store; store.open(root.path(), 7);
    store.mergeDirectory({{"conversations", QJsonArray{QJsonObject{{"id", 20}, {"type", "group"},
        {"group_state", "active"}, {"group_revision", "1"}, {"membership_epoch", "1"}}}}});
    for (int chat : {10, 20}) {
        QVector<StoredMessage> rows;
        for (int id = 1; id <= 3; ++id) {
            StoredMessage row;
            row.chatId = chat; row.messageId = id; row.senderId = id == 3 ? 7 : 8;
            row.recipientId = chat == 20 ? 0 : (id == 3 ? 8 : 7);
            row.content = "notice"; row.sentAt = id * 1000;
            row.clientMessageId = QString("%1-%2").arg(chat).arg(id);
            rows.append(row);
        }
        const QString epoch = chat == 20 ? "1" : "";
        store.applySyncPage(chat, 0, 3, rows, epoch);
        QCOMPARE(store.conversationAttention(chat).first().count, qint64(2));
        QVERIFY_EXCEPTION_THROWN(store.applySyncPage(chat, 3, 3, rows, epoch), std::exception);
        QCOMPARE(store.conversationAttention(chat).first().count, qint64(2));
        const auto before = store.history(chat, 0, 50).messages.back().localId;
        auto next = rows.front(); next.messageId = 4; next.clientMessageId += "-new";
        store.applySyncPage(chat, 3, 4, {next}, epoch);
        store.markConversationSeen(chat, before);
        QCOMPARE(store.conversationAttention(chat).first().count, qint64(1));
        store.markConversationSeen(chat, 0);
        QCOMPARE(store.conversationAttention(chat).first().count, qint64(1));
    }
    store.close(); store.open(root.path(), 7);
    QCOMPARE(store.conversationAttention(10).first().count, qint64(1));
    QCOMPARE(store.conversationAttention(20).first().count, qint64(1));
    store.markConversationSeen(10, store.history(10, 0, 50).messages.back().localId);
    QCOMPARE(store.conversationAttention(10).first().count, qint64(0));
    QCOMPARE(store.conversationAttention(20).first().count, qint64(1));
    store.close(); store.open(otherRoot.path(), 9);
    QVERIFY(store.conversationAttention().isEmpty());
    QCOMPARE(store.conversationAttention(10).first().count, qint64(0));
}

void MessageStorageTests::schemaFourAttentionUpgrade()
{
    QTemporaryDir root;
    StoredMessage row;
    row.chatId = 10; row.messageId = 1; row.senderId = 8; row.recipientId = 7;
    row.content = "preserved"; row.clientMessageId = "schema-four"; row.sentAt = 1000;
    {
        LocalMessageStore store; store.open(root.path(), 7);
        store.applySyncPage(10, 0, 1, {row});
    }
    const auto connection = QStringLiteral("attention-upgrade-fixture");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(root.path() + "/messages.sqlite"); QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("DROP TABLE conversation_attention"));
        QVERIFY(query.exec("PRAGMA user_version=4"));
    }
    QSqlDatabase::removeDatabase(connection);
    LocalMessageStore store; store.open(root.path(), 7);
    QVERIFY(QFile::exists(root.path() + "/messages.schema4.sqlite"));
    QCOMPARE(store.history(10, 0, 50).messages.first().content, QString("preserved"));
    QCOMPARE(store.cursor(10), qint64(1));
    QCOMPARE(store.conversationAttention(10).first().count, qint64(0));
    row.messageId = 2; row.clientMessageId = "after-upgrade";
    store.applySyncPage(10, 1, 2, {row});
    QCOMPARE(store.conversationAttention(10).first().count, qint64(1));
}

void MessageStorageTests::groupEpochAndLocalSearch()
{
    QTemporaryDir directory;
    LocalMessageStore store; store.open(directory.path(),7);
    QJsonObject group{{"id",12},{"type","group"},{"name","Group"},{"group_state","active"},
        {"membership_epoch","1"},{"group_revision","1"},{"joined_after_id",0}};
    store.mergeDirectory({{"conversations",QJsonArray{group}}});
    QJsonObject request{{"chat_id",12},{"chat_type","group"},{"from_uid",7},{"to_uid",0},{"membership_epoch","1"},
        {"text_array",QJsonArray{QJsonObject{{"msg_uuid","epoch-outbox"},{"msg_content","pending"}}}}};
    store.saveOutgoingRequest(request);
    StoredMessage row; row.chatId=12; row.senderId=8; row.recipientId=0; row.content="search needle"; row.sentAt=1000;
    QVector<StoredMessage> history;
    for (int i=1;i<=55;++i) { row.messageId=i; row.clientMessageId=QString("received-%1").arg(i); history.push_back(row); }
    store.applySyncPage(12,0,55,history,"1");
    auto first=store.search(12,"needle"); QCOMPARE(first.size(),50);
    auto second=store.search(12,"needle",first.back().localId); QCOMPARE(second.size(),5);
    QCOMPARE(store.search(13,"needle").size(),0);
    group["group_state"]="removed"; group["group_revision"]="2";
    store.mergeDirectory({{"conversations",QJsonArray{group}}});
    QVERIFY(store.dispatchDue(10000).isEmpty());
    QVERIFY_EXCEPTION_THROWN(store.saveOutgoingRequest(request),std::exception);
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12,55,55,{},"1"),std::exception);
    group["group_state"]="active"; group["membership_epoch"]="2"; group["group_revision"]="3"; group["joined_after_id"]=80;
    store.mergeDirectory({{"conversations",QJsonArray{group}}});
    store.retry("epoch-outbox"); QVERIFY(store.dispatchDue(10000).isEmpty());
    QVERIFY_EXCEPTION_THROWN(store.acceptSendResponse(QJsonObject{{"chat_id",12},{"error",0},{"membership_epoch","1"}}),std::exception);
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12,55,55,{},"1"),std::exception);
    row.messageId=70; row.clientMessageId="while-away";
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12,55,70,{row},"2"),std::exception);
    auto stale=group; stale["group_revision"]="2"; stale["group_state"]="removed";
    store.mergeDirectory({{"conversations",QJsonArray{stale}}});
    QCOMPARE(store.groupState(12)["group_state"].toString(),QString("active"));
    store.close(); store.open(directory.path(),7);
    QCOMPARE(store.cursor(12),55); QCOMPARE(store.history(12,0,100).messages.size(),56);
    QCOMPARE(store.groupState(12)["membership_epoch"].toString(),QString("2"));
}

void MessageStorageTests::groupMessagesPersistWithoutPrivateReceipts()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 7);
    const QJsonObject request{{"from_uid", 7}, {"to_uid", 0}, {"chat_id", 12}, {"chat_type", "group"}, {"membership_epoch", "1"},
        {"text_array", QJsonArray{QJsonObject{{"msg_uuid", "00000000-0000-4000-8000-000000000333"},
            {"msg_content", "group text"}}}}};
    QVERIFY_EXCEPTION_THROWN(store.saveOutgoingRequest(request), std::exception);
    store.mergeDirectory(QJsonObject{{"conversations", QJsonArray{QJsonObject{
        {"id", 12}, {"type", "group"}, {"group_state", "active"}, {"membership_epoch", "1"}, {"group_revision", "1"}, {"name", "Test group"}, {"uid", 0}}}}});
    store.saveOutgoingRequest(request);
    store.saveOutgoingRequest(request);
    StoredMessage sent;
    sent.chatId = 12; sent.senderId = 7; sent.recipientId = 0;
    sent.messageId = 100; sent.clientMessageId = request["text_array"].toArray().first().toObject()["msg_uuid"].toString();
    sent.content = "group text"; sent.sentAt = 1700000000000LL; sent.state = StoredMessage::Confirmed;
    auto received = sent;
    received.senderId = 8; received.messageId = 101;
    // Same UUID from another sender remains a distinct message.
    store.applySyncPage(12, 0, 101, {sent, received}, "1");
    QVERIFY(store.pendingReceipts(12).isEmpty());
    store.close(); store.open(directory.path(), 7);
    QCOMPARE(store.cursor(12), 101);
    QCOMPARE(store.directory()["conversations"].toArray().first().toObject()["type"].toString(), QString("group"));
    QCOMPARE(store.history(12, 0, 50).messages.size(), 2);
    auto conflict = sent; conflict.messageId = 102; conflict.content = "changed";
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12, 101, 102, {conflict}, "1"), std::exception);
    QCOMPARE(store.cursor(12), 101);
}

void MessageStorageTests::groupServiceSkipsReceipts()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy restored(&service, &MessageService::directoryRestored);
    QSignalSpy sync(&service, &MessageService::syncRequested);
    QSignalSpy receipts(&service, &MessageService::receiptRequested);
    service.start(directory.path(), 7, true);
    QTRY_COMPARE(restored.size(), 1);
    service.saveDirectory(QJsonObject{{"conversations",QJsonArray{QJsonObject{{"id",12},{"type","group"},{"name","Group"},
        {"group_state","active"},{"membership_epoch","1"},{"group_revision","1"}}}}});
    QTRY_COMPARE(sync.size(), 1);
    auto page = sync.first().first().toJsonObject();
    page["error"] = 0; page["msgs"] = QJsonArray{};
    page["next_cursor"] = 0; page["load_more"] = false;
    service.acceptSyncPage(page);
    QTRY_COMPARE_WITH_TIMEOUT(sync.size(), 2, 3500);
    service.synchronizeReceipts(12);
    service.observeRead(12, {100});
    QCoreApplication::processEvents();
    QCOMPARE(receipts.size(), 0);
    service.stop();
    service.registerChat(13, true);
    QCoreApplication::processEvents();
    QCOMPARE(sync.size(), 2);
}

/** 生成固定会话的存储消息，按服务端编号选择待发或确认状态。 */
static StoredMessage message(qint64 id, QString uuid = {})
{
    StoredMessage result;
    result.messageId = id;
    result.clientMessageId = uuid;
    result.chatId = 12;
    result.senderId = 7;
    result.recipientId = 8;
    result.content = QString::fromUtf8("你好\nmessage 😀");
    result.sentAt = 1700000000000LL + id;
    result.state = id > 0 ? StoredMessage::Confirmed : StoredMessage::Pending;
    return result;
}

void MessageStorageTests::schemaOneUpgradePreservesHistoryAndBackup()
{
    QTemporaryDir directory;
    const auto connection = QString("schema-one-fixture");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(directory.path() + "/messages.sqlite");
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE messages(local_id INTEGER PRIMARY KEY,message_id INTEGER,client_uuid TEXT,"
            "chat_id INTEGER,sender_id INTEGER,recipient_id INTEGER,content TEXT,sent_at INTEGER,state INTEGER,"
            "server_copy INTEGER DEFAULT 0)"));
        QVERIFY(query.exec("CREATE UNIQUE INDEX message_server_id ON messages(chat_id,message_id) WHERE message_id IS NOT NULL"));
        QVERIFY(query.exec("CREATE UNIQUE INDEX message_client_id ON messages(sender_id,client_uuid) WHERE client_uuid IS NOT NULL"));
        QVERIFY(query.exec("CREATE TABLE sync_state(chat_id INTEGER PRIMARY KEY,cursor INTEGER NOT NULL)"));
        QVERIFY(query.exec("INSERT INTO messages VALUES(1,10,'old',12,7,8,'preserved',1700000000000,1,1)"));
        QVERIFY(query.exec("INSERT INTO sync_state VALUES(12,10)"));
        QVERIFY(query.exec("PRAGMA user_version=1"));
    }
    QSqlDatabase::removeDatabase(connection);
    LocalMessageStore store;
    store.open(directory.path(), 8);
    QCOMPARE(store.cursor(12), 10);
    QCOMPARE(store.history(12, 0, 50).messages.first().content, QString("preserved"));
    QCOMPARE(store.history(12, 0, 50).messages.first().receipt, ReceiptLevel::None);
    QCOMPARE(store.pendingReceipts(12).size(), 1);
    QVERIFY(QFile::exists(directory.path() + "/messages.schema1.sqlite"));
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(directory.path() + "/messages.schema1.sqlite");
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("PRAGMA user_version"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec("SELECT content FROM messages WHERE message_id=10"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString("preserved"));
    }
    QSqlDatabase::removeDatabase(connection);
    store.close();
    store.open(directory.path(), 8);
    QCOMPARE(store.cursor(12), 10);
}

void MessageStorageTests::restartAndIncrementalCursor()
{
    QTemporaryDir directory;
    {
        LocalMessageStore store;
        store.open(directory.path());
        store.applySyncPage(12, 0, 20, {message(10), message(20)});
        // A later ACK must not skip an unseen interval.
        store.saveOutgoing({message(0, "later")});
        store.acknowledge(12, 7, "later", 40);
        QCOMPARE(store.cursor(12), 20);
    }
    LocalMessageStore reopened;
    reopened.open(directory.path());
    QCOMPARE(reopened.cursor(12), 20);
    reopened.applySyncPage(12, 20, 40, {message(30), message(40, "later")});
    const auto page = reopened.history(12, 0, 50);
    QCOMPARE(page.messages.size(), 4);
    QCOMPARE(page.messages.front().content, message(10).content);
    QCOMPARE(reopened.cursor(12), 40);
}

void MessageStorageTests::pendingHistoryAndAckMerge()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path());
    store.saveOutgoing({message(0, "a")});
    const auto localId = store.history(12, 0, 50).messages.front().localId;
    auto remote = message(5, "a");
    remote.senderId = 8;
    remote.recipientId = 7;
    store.applySyncPage(12, 0, 10, {remote, message(10, "a")});
    store.acknowledge(12, 7, "a", 10);
    QCOMPARE(store.history(12, 0, 50).messages.size(), 2);
    QCOMPARE(store.history(12, 0, 50).messages.back().localId, localId);
    store.saveOutgoing({message(0, "b")});
    store.close();
    store.open(directory.path());
    const auto rows = store.history(12, 0, 50).messages;
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.back().state, StoredMessage::Uncertain);
}

void MessageStorageTests::failedPageDoesNotAdvanceCursor()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path());
    store.applySyncPage(12, 0, 10, {message(10)});
    auto invalid = message(30);
    invalid.chatId = 99;
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12, 10, 30, {message(20), invalid}), std::exception);
    QCOMPARE(store.cursor(12), 10);
    QCOMPARE(store.history(12, 0, 50).messages.size(), 1);
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12, 0, 20, {message(20)}), std::exception);
    QCOMPARE(store.cursor(12), 10);
    store.applySyncPage(12, 10, 20, {message(20, "shared-uuid")});
    auto collision = message(30, "shared-uuid");
    collision.content = "@resource:v1:conflicting-server-identity";
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12, 20, 30, {collision}), std::exception);
    QCOMPARE(store.cursor(12), 20);
    QCOMPARE(store.history(12, 0, 50).messages.size(), 2);
}

void MessageStorageTests::accountsAndLocalPagination()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(UserStoragePaths::accountRoot(directory.path(), "http://one", 7));
    store.applySyncPage(12, 0, 30, {message(10), message(20), message(30)});
    const auto latest = store.history(12, 0, 2);
    QCOMPARE(latest.messages.front().messageId, 20);
    QVERIFY(latest.hasMore);
    const auto older = store.history(12, 20, 2);
    QCOMPARE(older.messages.size(), 1);
    QCOMPARE(older.messages.front().messageId, 10);
    QVERIFY(!older.hasMore);
    store.open(UserStoragePaths::accountRoot(directory.path(), "http://one", 8));
    QCOMPARE(store.cursor(12), 0);
    store.open(UserStoragePaths::accountRoot(directory.path(), "http://two", 7));
    QVERIFY(store.history(12, 0, 50).messages.isEmpty());
}

void MessageStorageTests::serviceSyncAndSessionIsolation()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy requests(&service, &MessageService::syncRequested);
    QSignalSpy changed(&service, &MessageService::messagesChanged);
    QSignalSpy failures(&service, &MessageService::failed);
    service.start(directory.path() + "/a", 7);
    service.registerChat(12);
    QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 5000);
    auto request = requests.takeFirst().at(0).toJsonObject();
    QJsonObject row{{"message_id", 10}, {"send_id", 8}, {"recv_id", 7},
        {"content", "server"}, {"created_at", 1700000000}, {"msg_uuid", "x"}};
    auto response = request;
    response["error"] = 0;
    response["msgs"] = QJsonArray{row};
    response["next_cursor"] = 10;
    response["load_more"] = false;
    service.acceptSyncPage(response);
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    service.synchronize(12);
    QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 5000);
    QCOMPARE(requests.first().at(0).toJsonObject()["after_id"].toInteger(), 10);
    service.stop();
    service.start(directory.path() + "/b", 8);
    service.registerChat(12);
    service.acceptSyncPage(response); // Old account response must be ignored.
    QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 2, 5000);
    QCOMPARE(requests.last().at(0).toJsonObject()["after_id"].toInteger(), 0);
    QVERIFY(failures.isEmpty());
    for (int count = 0; count < 256; ++count) service.loadHistory(12);
    service.start(directory.path() + "/c", 7);
    service.registerChat(12);
    const auto priorRequests = requests.size();
    QTRY_VERIFY_WITH_TIMEOUT((service.synchronize(12), requests.size() > priorRequests), 5000);
    QCOMPARE(requests.last().at(0).toJsonObject()["after_id"].toInteger(), 0);
}

void MessageStorageTests::committedSyncPageRemainsVisibleAfterFailure()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy requests(&service, &MessageService::syncRequested);
    QSignalSpy changed(&service, &MessageService::messagesChanged);
    QSignalSpy failed(&service, &MessageService::failed);
    QSignalSpy loaded(&service, &MessageService::historyLoaded);
    service.start(directory.path(), 7);
    service.registerChat(12);
    QTRY_COMPARE(requests.size(), 1);
    auto response = requests.takeFirst()[0].toJsonObject();
    response["error"] = 0;
    response["msgs"] = QJsonArray{QJsonObject{{"message_id", 10}, {"send_id", 8}, {"recv_id", 7},
        {"content", "committed first page"}, {"created_at", 1700000000}, {"msg_uuid", "first-page"}}};
    response["next_cursor"] = 10;
    response["load_more"] = true;
    service.acceptSyncPage(response);
    QTRY_COMPARE(requests.size(), 1);
    auto next = requests.takeFirst()[0].toJsonObject();
    next["error"] = 1;
    service.acceptSyncPage(next);
    QTRY_COMPARE(failed.size(), 1);
    // 第二页失败前，已落盘的第一页必须已通知 UI 刷新。
    QCOMPARE(changed.size(), 1);
    service.loadHistory(12);
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(qvariant_cast<QVector<StoredMessage>>(loaded.first()[2]).first().content,
        QString("committed first page"));
    service.synchronize(12);
    QTRY_COMPARE(requests.size(), 1);
    next = requests.takeFirst()[0].toJsonObject();
    QCOMPARE(next["after_id"].toInteger(), qint64(10));
    next["error"] = 0; next["msgs"] = QJsonArray{};
    next["next_cursor"] = 10; next["load_more"] = false;
    QSignalSpy synchronized(&service, &MessageService::synchronized);
    service.acceptSyncPage(next);
    QTRY_COMPARE(synchronized.size(), 1);
    QCOMPARE(changed.size(), 1);
}

void MessageStorageTests::outgoingRequiresDurableStorage()
{
    QTemporaryDir directory;
    const QJsonObject request{{"chat_id", 12}, {"from_uid", 7}, {"to_uid", 8},
        {"text_array", QJsonArray{QJsonObject{{"msg_uuid", "outgoing"}, {"msg_content", "durable"}}}}};
    {
        MessageService service;
        QSignalSpy sent(&service, &MessageService::sendRequested);
        service.start(directory.path() + "/valid", 7);
        service.send(request);
        QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 1, 5000);
    }
    LocalMessageStore store;
    store.open(directory.path() + "/valid");
    const auto rows = store.history(12, 0, 50).messages;
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.front().content, QString("durable"));
    QCOMPARE(rows.front().state, StoredMessage::Uncertain);
    store.close();

    QFile blocker(directory.path() + "/blocked");
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    MessageService service;
    QSignalSpy sent(&service, &MessageService::sendRequested);
    QSignalSpy failed(&service, &MessageService::sendFailed);
    service.start(blocker.fileName(), 7);
    service.send(request);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
    QVERIFY(sent.isEmpty());
    QVERIFY(blocker.remove());
    service.retry(12, "outgoing");
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 1, 5000);
    auto retried = sent.first()[0].toJsonObject();
    retried.remove("attempt_id");
    QCOMPARE(retried, request); // Recover the original intent, including resource payloads, without UI text.
}

void MessageStorageTests::refreshKeepsTheDisplayedIntervalComplete()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path());
    QVector<StoredMessage> first;
    for (int id = 1; id <= 50; ++id) first.push_back(message(id));
    store.applySyncPage(12, 0, 50, first);
    QVector<StoredMessage> next;
    for (int id = 51; id <= 160; ++id) next.push_back(message(id));
    store.applySyncPage(12, 50, 160, next);
    const auto refresh = store.history(12, 0, 50, 1);
    QCOMPARE(refresh.messages.size(), 160);
    for (int row = 0; row < refresh.messages.size(); ++row) QCOMPARE(refresh.messages[row].messageId, row + 1);
}


/** 生成带等级、时间和 revision 的回执响应夹具。 */
static QJsonObject receipt(qint64 id, int uid, int level, int revision)
{
    return {{"message_id", id}, {"recipient_uid", uid}, {"level", level == 2 ? "read" : "delivered"},
        {"revision", QString::number(revision)}, {"delivered_at", 1700000000000LL},
        {"read_at", level == 2 ? QJsonValue(1700000000001LL) : QJsonValue()}};
}

void MessageStorageTests::receiptsAreDurableMonotonicAndIndependent()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 7);
    store.acceptReceipts(12, {receipt(10, 8, 2, 2)}, 0, 2); // Receipt can precede the message and ACK.
    QCOMPARE(store.cursor(12), 0);
    store.applySyncPage(12, 0, 10, {message(10, "receipt-first")});
    QCOMPARE(store.history(12, 0, 50).messages.front().receipt, ReceiptLevel::Read);
    store.acceptReceipts(12, {receipt(10, 8, 1, 1)}); // A delayed report cannot downgrade read.
    store.close();
    store.open(directory.path(), 7);
    QCOMPARE(store.receiptCursor(12), 2);
    QCOMPARE(store.history(12, 0, 50).messages.front().receipt, ReceiptLevel::Read);
    QVERIFY_EXCEPTION_THROWN(store.observeRead(12, {10}), std::exception); // Cannot read your own outgoing message.
}

void MessageStorageTests::deliveredAckCannotEraseNewReadIntent()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 8);
    store.applySyncPage(12, 0, 10, {message(10)});
    QCOMPARE(store.pendingReceipts(12).first().toObject()["level"].toString(), QString("delivered"));
    store.observeRead(12, {10});
    store.acceptReceipts(12, {receipt(10, 8, 1, 1)});
    QCOMPARE(store.pendingReceipts(12).first().toObject()["level"].toString(), QString("read"));
    store.close();
    store.open(directory.path(), 8);
    QCOMPARE(store.pendingReceipts(12).size(), 1);
    store.acceptReceipts(12, {receipt(10, 8, 2, 2)});
    QVERIFY(store.pendingReceipts(12).isEmpty());
    QVERIFY_EXCEPTION_THROWN(store.observeRead(12, {999}), std::exception);
}

void MessageStorageTests::outgoingBatchRetriesUseStableIdentityAndAttempt()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 7);
    const QJsonObject request{{"chat_id", 12}, {"from_uid", 7}, {"to_uid", 8},
        {"text_array", QJsonArray{QJsonObject{{"msg_uuid", "a"}, {"msg_content", "immutable"}}}}};
    store.saveOutgoingRequest(request);
    store.saveOutgoingRequest(request);
    auto first = store.dispatchDue(0);
    QCOMPARE(first.size(), 1);
    QCOMPARE(first.first()["attempt_id"].toString(), QString("1"));
    QVERIFY(store.dispatchDue(14999).isEmpty());
    QVERIFY(store.dispatchDue(15000).isEmpty());
    QCOMPARE(store.history(12, 0, 50).messages.front().state, StoredMessage::Uncertain);
    auto second = store.dispatchDue(16000);
    QCOMPARE(second.size(), 1);
    auto original = first.first(); original.remove("attempt_id");
    auto retried = second.first(); retried.remove("attempt_id");
    QCOMPARE(original, request);
    QCOMPARE(original, retried);
    store.acceptSendResponse({{"error", 1}, {"chat_id", 12}, {"attempt_id", "1"},
        {"commit_error", "Conflict"}, {"client_msg_uuids", QJsonArray{"a"}}});
    QCOMPARE(store.history(12, 0, 50).messages.front().state, StoredMessage::Pending);
    auto ack = QJsonObject{{"error", 0}, {"chat_id", 12}, {"from_uid", 7}, {"to_uid", 8},
        {"uuid_msgId", QJsonArray{QJsonObject{{"msg_uuid", "a"}, {"message_id", 10}}}}};
    store.acceptSendResponse(ack);
    QCOMPARE(store.history(12, 0, 50).messages.front().state, StoredMessage::Confirmed);
    QVERIFY(store.dispatchDue(100000).isEmpty());
    QCOMPARE(store.cursor(12), 0);
    store.saveOutgoingRequest(request);
    QVERIFY(store.dispatchDue(100001).isEmpty());
    auto conflict = request; conflict["text_array"] = QJsonArray{QJsonObject{{"msg_uuid", "a"}, {"msg_content", "changed"}}};
    QVERIFY_EXCEPTION_THROWN(store.saveOutgoingRequest(conflict), std::exception);
    auto conflictingAck = ack;
    conflictingAck["uuid_msgId"] = QJsonArray{QJsonObject{{"msg_uuid", "a"}, {"message_id", 11}}};
    QVERIFY_EXCEPTION_THROWN(store.acceptSendResponse(conflictingAck), std::exception);
}

void MessageStorageTests::resourceIntentSurvivesRecoveryAndRetryBudget()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 7);
    const QString descriptor = "@resource:v1:{\"resource_id\":\"original-resource\",\"name\":\"original.png\"}";
    const QJsonObject request{{"chat_id", 12}, {"from_uid", 7}, {"to_uid", 8}, {"resource_id", "original-resource"},
        {"text_array", QJsonArray{QJsonObject{{"msg_uuid", "resource-uuid"}, {"msg_content", descriptor}}}}};
    store.saveOutgoingRequest(request);
    QCOMPARE(store.dispatchDue(0).size(), 1);
    QSet<int> changed;
    QVERIFY(store.dispatchDue(15000, {}, &changed).isEmpty());
    QVERIFY(changed.contains(12));
    QCOMPARE(store.dispatchDue(16000).first()["attempt_id"].toString(), QString("2"));
    QVERIFY(store.dispatchDue(31000).isEmpty());
    QVERIFY(store.dispatchDue(33999).isEmpty());
    QCOMPARE(store.dispatchDue(34000).first()["attempt_id"].toString(), QString("3"));
    QVERIFY(store.dispatchDue(49000).isEmpty());
    QCOMPARE(store.dispatchDue(59000).first()["attempt_id"].toString(), QString("4"));
    QVERIFY(store.dispatchDue(74000).isEmpty());
    QVERIFY(store.dispatchDue(999999).isEmpty()); // No fifth automatic attempt in this cycle.
    store.close();
    store.open(directory.path(), 7);
    QCOMPARE(store.recoveryChats(), QSet<int>{12});
    store.resumeOutgoing();
    QVERIFY(store.dispatchDue(1000000, {12}).isEmpty()); // Wait for recovery verification.
    auto replay = store.dispatchDue(1000000).first();
    QCOMPARE(replay.take("attempt_id").toString(), QString("5"));
    QCOMPARE(replay, request);
    auto canonical = message(10, "resource-uuid");
    canonical.content = "@resource:v1:{\"name\":\"original.png\",\"resource_id\":\"original-resource\"}";
    auto forged = canonical;
    forged.content.replace("original-resource", "wrong-resource");
    QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12, 0, 10, {forged}), std::exception);
    store.applySyncPage(12, 0, 10, {canonical});
    QVERIFY(store.dispatchDue(2000000).isEmpty());
    QCOMPARE(store.history(12, 0, 50).messages.first().state, StoredMessage::Confirmed);
}

void MessageStorageTests::uploadedResourceCanonicalHistory()
{
    for (bool group : {false, true}) {
        QTemporaryDir root;
        LocalMessageStore store; store.open(root.path(), 7);
        const QString epoch = group ? "1" : "";
        if (group) store.mergeDirectory({{"conversations", QJsonArray{QJsonObject{{"id", 12},
            {"type", "group"}, {"group_state", "active"}, {"group_revision", "1"}, {"membership_epoch", epoch}}}}});
        QJsonObject canonical{{"resource_id", "resource-1"}, {"name", "file.txt"}, {"media_type", "file"},
            {"sha256", QString(64, 'a')}, {"size", 75000}};
        auto uploaded = canonical;
        uploaded["offset"] = 75000; uploaded["owner"] = 7; uploaded["ready"] = true; uploaded["upload_id"] = "upload-1";
        const auto content = /** @brief 生成实际消息资源前缀及紧凑 JSON。 */ [](const QJsonObject &value) {
            return "@resource:v1:" + QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
        };
        auto pending = message(0, "upload-uuid"); pending.recipientId = group ? 0 : 8;
        pending.content = content(uploaded); store.saveOutgoing({pending});
        store.acknowledge(12, 7, pending.clientMessageId, 10);
        store.close(); store.open(root.path(), 7);
        auto confirmed = pending; confirmed.messageId = 10; confirmed.content = content(canonical);
        auto followup = message(11, "after-upload"); followup.senderId = 8; followup.recipientId = group ? 0 : 7;
        for (const QString field : {"resource_id", "name", "media_type", "sha256", "size", "unexpected"}) {
            auto forged = confirmed; auto altered = canonical;
            altered[field] = field == "size" ? QJsonValue(75001) : QJsonValue("different");
            forged.content = content(altered);
            QVERIFY_EXCEPTION_THROWN(store.applySyncPage(12, 0, 11, {forged, followup}, epoch), std::exception);
            QCOMPARE(store.cursor(12), qint64(0));
            QCOMPARE(store.history(12, 0, 50).messages.size(), 1);
        }
        store.applySyncPage(12, 0, 11, {confirmed, followup}, epoch);
        QCOMPARE(store.cursor(12), qint64(11));
        const auto rows = store.history(12, 0, 50).messages;
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.first().content, content(canonical));
        store.close(); store.open(root.path(), 7);
        QCOMPARE(store.cursor(12), qint64(11));
        QCOMPARE(store.history(12, 0, 50).messages.size(), 2);
    }
}

void MessageStorageTests::invalidReceiptPageRollsBackAndDoesNotAdvance()
{
    QTemporaryDir directory;
    LocalMessageStore store;
    store.open(directory.path(), 7);
    store.applySyncPage(12, 0, 20, {message(10), message(20)});
    QVERIFY_EXCEPTION_THROWN(store.acceptReceipts(12, {receipt(10, 8, 1, 1), receipt(20, 9, 2, 2)}, 0, 2), std::exception);
    QCOMPARE(store.receiptCursor(12), 0);
    QCOMPARE(store.history(12, 0, 50).messages.front().receipt, ReceiptLevel::None);
    QVERIFY_EXCEPTION_THROWN(store.acceptReceipts(12, {receipt(10, 8, 1, 2), receipt(20, 8, 1, 1)}, 0, 2), std::exception);
    store.acceptReceipts(12, {receipt(10, 8, 1, 1)}, 0, 1);
    QVERIFY_EXCEPTION_THROWN(store.acceptReceipts(12, {}, 0, 0), std::exception);
    QCOMPARE(store.receiptCursor(12), 1);
}

void MessageStorageTests::serviceReceiptRoundTripAndAccountIsolation()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy requests(&service, &MessageService::receiptRequested);
    QSignalSpy sync(&service, &MessageService::syncRequested);
    QSignalSpy changed(&service, &MessageService::messagesChanged);
    service.start(directory.path(), 8, true);
    service.registerChat(12);
    QTRY_COMPARE_WITH_TIMEOUT(sync.size(), 1, 5000);
    auto page = sync.first()[0].toJsonObject();
    page["error"] = 0; page["next_cursor"] = 10; page["load_more"] = false;
    page["msgs"] = QJsonArray{QJsonObject{{"message_id", 10}, {"send_id", 7}, {"recv_id", 8},
        {"content", "incoming"}, {"created_at", 1700000000}, {"msg_uuid", "x"}}};
    service.acceptSyncPage(page);
    QTRY_VERIFY_WITH_TIMEOUT(requests.size() >= 2, 5000);
    QJsonObject report;
    for (const auto &entry : requests) if (entry[0].toUInt() == 1029) report = entry[1].toJsonObject();
    QVERIFY(!report.isEmpty());
    QCOMPARE(report["items"].toArray().first().toObject()["level"].toString(), QString("delivered"));
    service.observeRead(12, {10});
    report["error"] = 0; report["items"] = QJsonArray{receipt(10, 8, 1, 1)};
    service.acceptReceiptResponse(report);
    QTRY_VERIFY_WITH_TIMEOUT(requests.size() >= 3, 5000);
    QCOMPARE(requests.last()[1].toJsonObject()["items"].toArray().first().toObject()["level"].toString(), QString("read"));
    service.stop(); service.start(directory.path() + "/other", 7, true);
    const auto count = changed.size();
    service.acceptReceiptResponse(report);
    service.loadHistory(12);
    QSignalSpy loaded(&service, &MessageService::historyLoaded);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QCOMPARE(changed.size(), count);
    QVERIFY(qvariant_cast<QVector<StoredMessage>>(loaded.first()[2]).isEmpty());
}

void MessageStorageTests::directoryPersistenceAndPagination()
{
    QTemporaryDir root;
    LocalMessageStore store;
    store.open(root.path(), 8);
    QJsonArray contacts;
    for (int id = 1; id <= 40; ++id) contacts.append(QJsonObject{{"id", id}, {"uid", id}, {"name", QString::number(id)}});
    const QJsonObject application{{"id", 7}, {"fromuid", 7}, {"status", 0}};
    store.mergeDirectory({{"contacts", contacts}, {"applications", QJsonArray{application}}});
    store.mergeDirectory({{"contacts", QJsonArray{QJsonObject{{"id", 7}, {"uid", 7}, {"name", "updated"}}}},
        {"conversations", QJsonArray{QJsonObject{{"id", 12}, {"uid", 7}, {"type", "private"}}}}, {"approved_uid", 7}});
    store.close();
    store.open(root.path(), 8);
    QCOMPARE(store.directory()["contacts"].toArray().size(), 40);
    QCOMPARE(store.directory()["applications"].toArray().first().toObject()["status"].toInt(), 1);
    QCOMPARE(store.directory()["conversations"].toArray().size(), 1);
    int cursor = 0, count = 0;
    QSet<int> seen;
    while (true) {
        const auto page = store.directoryPage("contacts", cursor, 13);
        if (page.isEmpty()) break;
        for (const auto &row : page) {
            const int id = row.toObject()["id"].toInt();
            QVERIFY(!seen.contains(id));
            seen.insert(id);
            ++count;
        }
        cursor = page.last().toObject()["id"].toInt();
    }
    QCOMPARE(count, 40);
    store.mergeDirectory({{"contacts",QJsonArray{QJsonObject{{"id",7},{"backname","我的备注"}}}}});
    QCOMPARE(store.findDirectory("contacts","我的备注",0,50).size(),1);
    QCOMPARE(store.findDirectory("contacts","updated",0,50).first().toObject()["backname"].toString(),QString("我的备注"));
    QCOMPARE(store.findDirectory("contacts","40",0,50).first().toObject()["id"].toInt(),40);
    QCOMPARE(store.findDirectory("contacts","",0,13).size(),13);
    QCOMPARE(store.findDirectory("contacts","",13,13).first().toObject()["id"].toInt(),14);
    QCOMPARE(store.findDirectory("conversations","",0,50).size(),0);
    QVERIFY_EXCEPTION_THROWN(store.mergeDirectory({{"contacts", QJsonArray{
        QJsonObject{{"id", 41}}, QJsonObject{{"id", 0}}}}}), std::runtime_error);
    QCOMPARE(store.directory()["contacts"].toArray().size(), 40);
    store.mergeDirectory({});
    QCOMPARE(store.directory()["contacts"].toArray().size(), 40);
}

void MessageStorageTests::directoryServiceIsolation()
{
    QTemporaryDir first, second;
    MessageService service;
    QSignalSpy restored(&service, &MessageService::directoryRestored);
    QSignalSpy changed(&service, &MessageService::directoryChanged);
    QSignalSpy pages(&service, &MessageService::directoryPageLoaded);
    QSignalSpy failed(&service, &MessageService::directoryFailed);
    service.start(first.path(), 8);
    QTRY_COMPARE(restored.size(), 1);
    service.saveDirectory({{"contacts", QJsonArray{QJsonObject{{"id", 7}, {"uid", 7}}}}});
    QTRY_COMPARE(changed.size(), 1);
    service.loadDirectoryPage("contacts", 0, 13);
    QTRY_COMPARE(pages.size(), 1);
    QCOMPARE(pages.first()[2].toJsonArray().size(), 1);
    QVERIFY(!pages.first()[3].toBool());
    service.saveDirectory({{"contacts", QJsonArray{QJsonObject{{"id", 0}}}}});
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(changed.size(), 1);
    service.saveDirectory({{"contacts", QJsonArray{QJsonObject{{"id", 9}, {"uid", 9}}}}});
    service.start(second.path(), 10);
    QTRY_COMPARE(restored.size(), 2);
    QVERIFY(restored.last()[0].toJsonObject()["contacts"].toArray().isEmpty());
    QCOMPARE(changed.size(), 1);
    service.start(first.path(), 8);
    QTRY_COMPARE(restored.size(), 3);
    QCOMPARE(restored.last()[0].toJsonObject()["contacts"].toArray().size(), 2);
}

void MessageStorageTests::groupOperationSurvivesRefreshAndRestart()
{
    QTemporaryDir directory;
    MessageService service;
    QSignalSpy restored(&service,&MessageService::directoryRestored);
    QSignalSpy changed(&service,&MessageService::directoryChanged);
    service.start(directory.path(),7); QTRY_COMPARE(restored.size(),1);
    QJsonObject row{{"id",12},{"type","group"},{"name","group"},{"group_state","active"},
        {"membership_epoch","1"},{"group_revision","1"}};
    service.saveDirectory({{"conversations",QJsonArray{row}}}); QTRY_COMPARE(changed.size(),1);
    row["group_revision"]="2"; row["name"]="renamed";
    service.saveDirectory({{"conversations",QJsonArray{row}}});
    const QJsonObject command{{"request_id","immutable-request"},{"expected_revision","1"},{"operation","leave"}};
    service.saveGroupOperation(12,command); QTRY_COMPARE(changed.size(),3);
    QCOMPARE(service.groupState(12)["group_revision"].toString(),QString("2"));
    QCOMPARE(service.groupState(12)["pending_group_operation"].toObject(),command);
    service.start(directory.path(),7); QTRY_COMPARE(restored.size(),2);
    QCOMPARE(service.groupState(12)["pending_group_operation"].toObject(),command);
    service.saveGroupOperation(12,{}); QTRY_COMPARE(changed.size(),4);
    service.start(directory.path(),7); QTRY_COMPARE(restored.size(),3);
    QVERIFY(service.groupState(12)["pending_group_operation"].toObject().isEmpty());
}

void MessageStorageTests::schemaTwoDirectoryUpgrade()
{
    QTemporaryDir root;
    LocalMessageStore store;
    store.open(root.path(), 8);
    store.applySyncPage(12, 0, 10, {message(10, "preserved")});
    store.close();
    const QString connection = "directory-upgrade-fixture";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(root.path() + "/messages.sqlite");
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("DROP TABLE contacts"));
        QVERIFY(query.exec("DROP TABLE conversations"));
        QVERIFY(query.exec("DROP TABLE applications"));
        QVERIFY(query.exec("DROP TABLE conversation_attention"));
        QVERIFY(query.exec("PRAGMA user_version=2"));
    }
    QSqlDatabase::removeDatabase(connection);
    store.open(root.path(), 8);
    QCOMPARE(store.cursor(12), 10);
    QCOMPARE(store.history(12, 0, 50).messages.size(), 1);
    QVERIFY(QFile::exists(root.path() + "/messages.schema2.sqlite"));
    store.mergeDirectory({{"contacts", QJsonArray{QJsonObject{{"id", 7}}}}});
    QCOMPARE(store.directory()["contacts"].toArray().size(), 1);
    store.close();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(root.path() + "/messages.sqlite");
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("DROP TABLE conversation_attention"));
        QVERIFY(query.exec("PRAGMA user_version=3"));
    }
    QSqlDatabase::removeDatabase(connection);
    store.open(root.path(),8);
    QVERIFY(QFile::exists(root.path()+"/messages.schema3.sqlite"));
    QCOMPARE(store.cursor(12),10);
    QCOMPARE(store.directory()["contacts"].toArray().size(),1);
}


void MessageStorageTests::socialVersionsAndSummaries()
{
    QTemporaryDir root; LocalMessageStore store; store.open(root.path(),7);
    QJsonObject relation{{"id",12},{"uid",8},{"type","private"},{"relationship_active",true},{"relationship_revision","1"}};
    store.mergeDirectory({{"conversations",QJsonArray{relation}}, {"contacts",QJsonArray{QJsonObject{
        {"id",8},{"uid",8},{"name","new name"},{"description","new description"},{"profile_revision","3"},
        {"relationship_active",true},{"relationship_revision","1"}}}}});
    QJsonObject request{{"chat_id",12},{"from_uid",7},{"to_uid",8},{"relationship_revision","1"},
        {"text_array",QJsonArray{QJsonObject{{"msg_uuid","00000000-0000-4000-8000-000000000991"},{"msg_content","retained"}}}}};
    store.saveOutgoingRequest(request);
    relation["relationship_active"] = false; relation["relationship_revision"] = "2";
    store.mergeDirectory({{"conversations",QJsonArray{relation}},{"contacts",QJsonArray{QJsonObject{
        {"id",8},{"name","old name"},{"profile_revision","1"},{"relationship_active",false},{"relationship_revision","2"}}}}});
    QVERIFY(store.dispatchDue(QDateTime::currentMSecsSinceEpoch()+60000).isEmpty());
    QVERIFY_EXCEPTION_THROWN(store.saveOutgoingRequest(request),std::exception);
    QCOMPARE(store.directory()["contacts"].toArray().first().toObject()["name"].toString(),QString("new name"));
    QVERIFY(store.findDirectory("contacts","",0,50).isEmpty());
    relation["relationship_active"] = true; relation["relationship_revision"] = "3";
    store.mergeDirectory({{"conversations",QJsonArray{relation}}});
    store.resumeOutgoing();
    QVERIFY(store.dispatchDue(QDateTime::currentMSecsSinceEpoch()+120000).isEmpty());
    QCOMPARE(store.history(12,0,50).messages.size(),1);
    store.close(); store.open(root.path(),7);
    QVERIFY(store.dispatchDue(QDateTime::currentMSecsSinceEpoch()+180000).isEmpty());
    // New explicit submissions use the current version, while stale directory data cannot revive old work.
    relation["relationship_revision"] = "1";
    store.mergeDirectory({{"conversations",QJsonArray{relation}}});
    request["relationship_revision"] = "3";
    request["text_array"] = QJsonArray{QJsonObject{{"msg_uuid","00000000-0000-4000-8000-000000000992"},{"msg_content","current"}}};
    store.saveOutgoingRequest(request);
    QCOMPARE(store.dispatchDue(QDateTime::currentMSecsSinceEpoch()+240000).size(),1);
    for (int id=20;id<40;++id) {
        store.mergeDirectory({{"conversations",QJsonArray{QJsonObject{{"id",id},{"type","private"}}}}});
        StoredMessage row; row.chatId=id; row.senderId=8; row.recipientId=7; row.messageId=id;
        row.content=QString("message %1").arg(id); row.sentAt=id*1000;
        store.applySyncPage(id,0,id,{row});
    }
    auto summaries = store.conversationSummaries();
    QCOMPARE(summaries.size(),21);
    QCOMPARE(summaries[1].toObject()["chat_id"].toInt(),39);
    auto row = message(50); row.chatId=20; row.sentAt=QDateTime::currentMSecsSinceEpoch()+10000;
    row.content="@resource:v1:{\"name\":\"photo.png\"}";
    store.applySyncPage(20,20,50,{row});
    summaries=store.conversationSummaries();
    QCOMPARE(summaries.first().toObject()["chat_id"].toInt(),20);
    QCOMPARE(summaries.first().toObject()["summary"].toString(),QString("photo.png"));
    store.close(); store.open(root.path(),7);
    QCOMPARE(store.conversationSummaries().first().toObject()["chat_id"].toInt(),20);
}

QTEST_GUILESS_MAIN(MessageStorageTests)
#include "message_storage_tests.moc"
