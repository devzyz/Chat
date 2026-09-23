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
};

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

QTEST_GUILESS_MAIN(MessageStorageTests)
#include "message_storage_tests.moc"
