#include "localmessagestore.h"

#include <QDir>
#include <QDateTime>
#include <QMap>
#include <climits>
#include <QJsonDocument>
#include <QSet>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <stdexcept>

namespace {
/** @brief 检查存储操作返回值，失败时抛带操作说明的运行时异常。 */
void require(bool success, const char *operation)
{
    if (!success) throw std::runtime_error(operation);
}

/** @brief 比较文本或资源 JSON 的规范内容，资源需具有非空资源 ID。 */
bool sameMessageContent(const QString &original, const QString &canonical)
{
    if (original == canonical) return true;
    if (!original.startsWith("@resource:v1:") || !canonical.startsWith("@resource:v1:")) return false;
    const auto left = QJsonDocument::fromJson(original.mid(13).toUtf8());
    const auto right = QJsonDocument::fromJson(canonical.mid(13).toUtf8());
    if (!left.isObject() || !right.isObject() || left.object().value("resource_id").toString().isEmpty())
        return false;
    auto submitted = left.object();
    auto received = right.object();
    // 上传进度不属于消息内容；仅移除已知临时字段，其他业务字段或未知字段差异仍拒绝。
    for (const QString field : {"offset", "owner", "ready", "upload_id"}) {
        submitted.remove(field);
        received.remove(field);
    }
    return submitted == received;
}

/** @brief 准备并绑定 SQL 参数后同步执行，失败抛异常，返回可读取结果的查询对象。 */
QSqlQuery query(QSqlDatabase &db, const QString &sql, const QVariantList &values = {})
{
    QSqlQuery result(db);
    require(result.prepare(sql), "Cannot prepare local message query");
    for (const auto &value : values) result.addBindValue(value);
    require(result.exec(), "Local message database operation failed");
    return result;
}

/** @brief 借用 SQLite 连接管理一次事务；未提交时析构回滚，回滚失败则关闭连接。 */
class Transaction {
public:
    /** @brief 借用连接并立即开启事务，开启失败抛异常。 */
    explicit Transaction(QSqlDatabase &db) : _db(db) { require(_db.transaction(), "Cannot start transaction"); }
    /** @brief 禁止复制或移动事务，避免多个对象提交或回滚同一连接。 */
    Transaction(const Transaction&) = delete;
    /** @brief 禁止事务赋值，避免改变正在管理的连接及提交状态。 */
    Transaction& operator=(const Transaction&) = delete;
    /** @brief 禁止复制或移动事务，避免多个对象提交或回滚同一连接。 */
    Transaction(Transaction&&) = delete;
    /** @brief 禁止事务赋值，避免改变正在管理的连接及提交状态。 */
    Transaction& operator=(Transaction&&) = delete;
    /** @brief 未提交时回滚；回滚失败关闭连接，防止再次使用未知事务状态。 */
    ~Transaction() {
        if (!_committed && !_db.rollback()) {
            // Never reuse a connection whose transaction outcome is unknown.
            qWarning("Local message rollback failed; closing connection");
            _db.close();
        }
    }
    /** @brief 提交当前 SQLite 事务，成功才禁止析构回滚；失败抛异常。 */
    void commit() { require(_db.commit(), "Cannot commit local messages"); _committed = true; }
private:
    QSqlDatabase &_db;
    bool _committed = false;
};

/** @brief 按固定查询列顺序构造持久化消息值。 */
StoredMessage readMessage(const QSqlQuery &row)
{
    StoredMessage message;
    message.localId = row.value(0).toLongLong();
    message.messageId = row.value(1).toLongLong();
    message.clientMessageId = row.value(2).toString();
    message.chatId = row.value(3).toInt();
    message.senderId = row.value(4).toInt();
    message.recipientId = row.value(5).toInt();
    message.content = row.value(6).toString();
    message.sentAt = row.value(7).toLongLong();
    message.state = static_cast<StoredMessage::State>(row.value(8).toInt());
    return message;
}
const QString columns = "local_id,message_id,client_uuid,chat_id,sender_id,recipient_id,content,sent_at,state";
/** @brief 将 JSON 字符串解析为非负 qint64 revision，非规范十进制或越界时抛异常。 */
qint64 revisionValue(const QJsonValue &value) {
    const auto text = value.toString();
    bool valid = false;
    const auto revision = text.toLongLong(&valid);
    require(valid && revision >= 0 && QString::number(revision) == text, "Invalid receipt revision");
    return revision;
}
}

void LocalMessageStore::queueDelivered(int chatId)
{
    if (_uid <= 0) return;
    query(_db, "INSERT INTO receipt_outbox(chat_id,message_id,level) "
        "SELECT chat_id,message_id,1 FROM messages m WHERE chat_id=? AND recipient_id=? AND message_id IS NOT NULL "
        "AND NOT EXISTS(SELECT 1 FROM message_receipts r WHERE r.chat_id=m.chat_id "
        "AND r.message_id=m.message_id AND r.recipient_uid=m.recipient_id) "
        "ON CONFLICT(chat_id,message_id) DO NOTHING", {chatId, _uid});
}

qint64 LocalMessageStore::receiptCursor(int chatId)
{
    auto row = query(_db, "SELECT revision FROM receipt_sync_state WHERE chat_id=?", {chatId});
    return row.next() ? row.value(0).toLongLong() : 0;
}

QJsonArray LocalMessageStore::pendingReceipts(int chatId)
{
    queueDelivered(chatId);
    auto rows = query(_db, "SELECT message_id,level FROM receipt_outbox WHERE chat_id=? AND rejected=0 ORDER BY message_id LIMIT 8", {chatId});
    QJsonArray items;
    while (rows.next()) items.append(QJsonObject{{"message_id", rows.value(0).toLongLong()},
        {"level", rows.value(1).toInt() == 2 ? "read" : "delivered"}});
    return items;
}

void LocalMessageStore::observeRead(int chatId, const QVector<qint64> &ids)
{
    require(_uid > 0 && ids.size() <= 256, "Invalid read observation");
    Transaction transaction(_db);
    for (auto id : ids) {
        auto row = query(_db, "SELECT 1 FROM messages WHERE chat_id=? AND message_id=? AND recipient_id=?",
            {chatId, id, _uid});
        require(row.next(), "Cannot read an unpersisted or outgoing message");
        query(_db, "INSERT INTO receipt_outbox(chat_id,message_id,level) VALUES(?,?,2) "
            "ON CONFLICT(chat_id,message_id) DO UPDATE SET level=2 WHERE rejected=0", {chatId, id});
    }
    transaction.commit();
}

void LocalMessageStore::acceptReceipts(int chatId, const QJsonArray &items, qint64 previous, qint64 next)
{
    Transaction transaction(_db);
    if (previous >= 0) require(previous == receiptCursor(chatId), "Stale receipt page");
    qint64 last = previous;
    QSet<qint64> ids;
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto id = item["message_id"].toInteger(-1);
        const auto uid = item["recipient_uid"].toInt(-1);
        const auto revision = revisionValue(item["revision"]);
        const auto name = item["level"].toString();
        const int level = name == "read" ? 2 : name == "delivered" ? 1 : 0;
        require(id > 0 && id <= INT_MAX && uid > 0 && level > 0 && revision > 0 && !ids.contains(id),
            "Invalid receipt identity");
        require(item["delivered_at"].toInteger() > 0 &&
            ((level == 2 && item["read_at"].toInteger() > 0) || (level == 1 && item["read_at"].isNull())),
            "Invalid receipt timestamps");
        ids.insert(id);
        auto message = query(_db, "SELECT recipient_id FROM messages WHERE chat_id=? AND message_id=?", {chatId, id});
        if (message.next()) require(message.value(0).toInt() == uid, "Receipt recipient conflict");
        auto prior = query(_db, "SELECT recipient_uid FROM message_receipts WHERE chat_id=? AND message_id=?", {chatId, id});
        if (prior.next()) require(prior.value(0).toInt() == uid, "Receipt identity conflict");
        if (previous >= 0) require(revision > last && revision <= next, "Invalid receipt page order");
        last = revision;
        query(_db, "INSERT INTO message_receipts(chat_id,message_id,recipient_uid,level,revision) VALUES(?,?,?,?,?) "
            "ON CONFLICT(chat_id,message_id,recipient_uid) DO UPDATE SET "
            "level=MAX(level,excluded.level),revision=MAX(revision,excluded.revision)", {chatId, id, uid, level, revision});
        if (uid == _uid) query(_db, "DELETE FROM receipt_outbox WHERE chat_id=? AND message_id=? AND level<=?", {chatId, id, level});
    }
    if (previous >= 0) {
        require(next == last, "Invalid receipt page cursor");
        query(_db, "INSERT INTO receipt_sync_state(chat_id,revision) VALUES(?,?) "
            "ON CONFLICT(chat_id) DO UPDATE SET revision=excluded.revision", {chatId, next});
    }
    transaction.commit();
}

void LocalMessageStore::discardReceipt(int chatId, qint64 messageId)
{
    query(_db, "UPDATE receipt_outbox SET rejected=1 WHERE chat_id=? AND message_id=?", {chatId, messageId});
}

void LocalMessageStore::clearCommittedBatches()
{
    query(_db, "DELETE FROM outgoing_batches WHERE NOT EXISTS (SELECT 1 FROM outgoing_items i JOIN messages m "
        "ON m.client_uuid=i.uuid WHERE i.batch_key=outgoing_batches.batch_key AND m.message_id IS NULL)");
    query(_db, "DELETE FROM outgoing_items WHERE batch_key NOT IN (SELECT batch_key FROM outgoing_batches)");
}

void LocalMessageStore::saveOutgoingRequest(const QJsonObject &request)
{
    const int chat = request["chat_id"].toInt();
    const int sender = request["from_uid"].toInt();
    const int recipient = request["to_uid"].toInt();
    const auto items = request["text_array"].toArray();
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    require(canSendGroup(request) && canSendPrivate(request), "Group membership changed; cannot send");
    require(chat > 0 && sender == _uid && (recipient > 0 || (recipient == 0 && request["chat_type"].toString() == "group" && isGroupChat(chat))) && !items.isEmpty() && bytes.size() <= 1950,
        "Invalid outgoing request");
    QVector<StoredMessage> messages;
    QSet<QString> uuids;
    for (const auto &item : items) {
        StoredMessage message;
        message.chatId = chat; message.senderId = sender; message.recipientId = recipient;
        message.clientMessageId = item.toObject()["msg_uuid"].toString();
        message.content = item.toObject()["msg_content"].toString();
        message.sentAt = QDateTime::currentMSecsSinceEpoch();
        require(!message.clientMessageId.isEmpty() && !uuids.contains(message.clientMessageId), "Invalid outgoing UUID");
        uuids.insert(message.clientMessageId);
        messages.push_back(message);
    }
    Transaction transaction(_db);
    const auto key = messages.first().clientMessageId;
    auto existing = query(_db, "SELECT payload FROM outgoing_batches WHERE batch_key=?", {key});
    if (existing.next()) {
        require(existing.value(0).toByteArray() == bytes, "Outgoing batch conflict");
        transaction.commit();
        return;
    }
    saveOutgoingRows(messages);
    query(_db, "INSERT INTO outgoing_batches(batch_key,chat_id,payload) VALUES(?,?,?)", {key, chat, bytes});
    for (const auto &uuid : uuids) {
        query(_db, "INSERT INTO outgoing_items(uuid,batch_key) VALUES(?,?)", {uuid, key});
        query(_db, "UPDATE messages SET state=? WHERE sender_id=? AND client_uuid=? AND message_id IS NULL",
            {StoredMessage::Queued, sender, uuid});
    }
    clearCommittedBatches();
    transaction.commit();
}

QSet<int> LocalMessageStore::recoveryChats()
{
    QSet<int> result;
    auto rows = query(_db, "SELECT DISTINCT chat_id FROM outgoing_batches WHERE paused=0");
    while (rows.next()) result.insert(rows.value(0).toInt());
    return result;
}

QVector<QJsonObject> LocalMessageStore::dispatchDue(qint64 now, const QSet<int> &recovering, QSet<int> *changed)
{
    Transaction transaction(_db);
    if (changed) {
        auto expired = query(_db, "SELECT DISTINCT chat_id FROM outgoing_batches WHERE in_flight=1 AND due<=?", {now});
        while (expired.next()) changed->insert(expired.value(0).toInt());
    }
    query(_db, "UPDATE messages SET state=? WHERE message_id IS NULL AND client_uuid IN "
        "(SELECT i.uuid FROM outgoing_items i JOIN outgoing_batches b ON b.batch_key=i.batch_key "
        "WHERE b.in_flight=1 AND b.due<=?)", {StoredMessage::Uncertain, now});
    query(_db, "UPDATE outgoing_batches SET in_flight=0,due=?+CASE retries WHEN 1 THEN 1000 WHEN 2 THEN 3000 ELSE 10000 END "
        "WHERE in_flight=1 AND due<=?", {now, now});
    QString filter;
    QVariantList values{now};
    for (int chat : recovering) { filter += " AND chat_id<>?"; values.push_back(chat); }
    auto rows = query(_db, "SELECT batch_key,payload,attempt FROM outgoing_batches "
        "WHERE paused=0 AND in_flight=0 AND retries<4 AND due<=?" + filter + " ORDER BY due,batch_key LIMIT 8", values);
    QVector<QJsonObject> result;
    while (rows.next()) {
        const auto key = rows.value(0).toString();
        const auto priorAttempt = rows.value(2).toLongLong();
        require(priorAttempt >= 0 && priorAttempt < LLONG_MAX, "Outgoing attempt counter exhausted");
        const auto facts = MessageStateReducer::reduce({SendPhase::Uncertain, ReceiptLevel::None, 0, priorAttempt},
            MessageStateReducer::Event::Dispatch, priorAttempt + 1);
        auto request = QJsonDocument::fromJson(rows.value(1).toByteArray()).object();
        if (!canSendGroup(request) || !canSendPrivate(request)) {
            query(_db, "UPDATE outgoing_batches SET paused=1,in_flight=0 WHERE batch_key=?", {key});
            query(_db, "UPDATE messages SET state=? WHERE message_id IS NULL AND state=? AND client_uuid IN "
                "(SELECT uuid FROM outgoing_items WHERE batch_key=?)", {StoredMessage::Uncertain, StoredMessage::Pending, key});
            continue;
        }
        request["attempt_id"] = QString::number(facts.attempt);
        query(_db, "UPDATE outgoing_batches SET in_flight=1,attempt=?,retries=retries+1,due=? WHERE batch_key=?",
            {facts.attempt, now + 15000, key});
        query(_db, "UPDATE messages SET state=? WHERE message_id IS NULL AND client_uuid IN "
            "(SELECT uuid FROM outgoing_items WHERE batch_key=?)", {StoredMessage::Pending, key});
        result.push_back(request);
        if (changed) changed->insert(request["chat_id"].toInt());
    }
    transaction.commit();
    return result;
}

void LocalMessageStore::retry(const QString &uuid)
{
    auto batch = query(_db, "SELECT 1 FROM outgoing_items WHERE uuid=?", {uuid});
    if (!batch.next()) {
        batch.finish();
        auto row = query(_db, "SELECT chat_id,recipient_id,content FROM messages "
            "WHERE client_uuid=? AND sender_id=? AND message_id IS NULL", {uuid, _uid});
        require(row.next(), "No recoverable outgoing intent");
        QJsonObject request{{"chat_id", row.value(0).toInt()}, {"from_uid", _uid}, {"to_uid", row.value(1).toInt()}};
        const auto content = row.value(2).toString();
        if (content.startsWith("@resource:v1:")) {
            const auto descriptor = QJsonDocument::fromJson(content.mid(13).toUtf8()).object();
            require(!descriptor["resource_id"].toString().isEmpty(), "Original resource descriptor unavailable");
            request["resource_id"] = descriptor["resource_id"];
        }
        request["text_array"] = QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"msg_content", content}}};
        row.finish();
        saveOutgoingRequest(request);
    }
    batch.finish();
    query(_db, "UPDATE outgoing_batches SET paused=0,retries=0,due=0 WHERE in_flight=0 AND batch_key="
        "(SELECT batch_key FROM outgoing_items WHERE uuid=?)", {uuid});
}

void LocalMessageStore::pauseOutgoing()
{
    query(_db, "UPDATE outgoing_batches SET paused=1,in_flight=0");
}

void LocalMessageStore::resumeOutgoing()
{
    query(_db, "UPDATE outgoing_batches SET retries=0,due=0,in_flight=0 WHERE paused=0");
}

void LocalMessageStore::acceptSendResponse(const QJsonObject &response)
{
    const int chat = response["chat_id"].toInt();
    require(chat > 0 && response["error"].isDouble(), "Invalid send response");
    if (isGroupChat(chat)) require(canSendGroup(response), "Stale group acknowledgement");
    QSet<QString> ids;
    for (const auto &id : response["client_msg_uuids"].toArray()) {
        require(id.isString() && !id.toString().isEmpty() && !ids.contains(id.toString()), "Invalid ACK UUIDs");
        ids.insert(id.toString());
    }
    const bool success = response["error"].toInt(-1) == 0;
    QMap<QString, qint64> acknowledgements;
    QSet<qint64> serverIds;
    if (success) {
        require(response["from_uid"].toInt() == _uid, "Invalid ACK sender");
        for (const auto &entry : response["uuid_msgId"].toArray()) {
            const auto row = entry.toObject();
            const auto uuid = row["msg_uuid"].toString();
            const auto id = row["message_id"].toInteger();
            require(!uuid.isEmpty() && id > 0 && id <= INT_MAX && !acknowledgements.contains(uuid)
                && !serverIds.contains(id), "Invalid ACK mapping");
            acknowledgements.insert(uuid, id); serverIds.insert(id);
        }
        require(!acknowledgements.isEmpty(), "Missing ACK mapping");
        const QSet<QString> mapped(acknowledgements.keyBegin(), acknowledgements.keyEnd());
        if (ids.isEmpty()) ids = mapped;
        require(ids == mapped, "Partial ACK mapping");
    }
    if (ids.isEmpty()) return;
    Transaction transaction(_db);
    auto row = query(_db, "SELECT b.batch_key,b.payload,b.attempt FROM outgoing_batches b JOIN outgoing_items i "
        "ON b.batch_key=i.batch_key WHERE i.uuid=? AND b.chat_id=?", {*ids.begin(), chat});
    if (!row.next()) {
        row.finish();
        if (success) {
            for (auto it = acknowledgements.cbegin(); it != acknowledgements.cend(); ++it) {
                auto known = query(_db, "SELECT message_id,recipient_id FROM messages "
                    "WHERE sender_id=? AND client_uuid=? AND chat_id=?", {_uid, it.key(), chat});
                require(known.next() && known.value(0).toLongLong() == it.value()
                    && known.value(1).toInt() == response["to_uid"].toInt(), "Unknown or conflicting ACK identity");
            }
        }
        transaction.commit();
        return;
    }
    const auto key = row.value(0).toString();
    const auto request = QJsonDocument::fromJson(row.value(1).toByteArray()).object();
    const auto attempt = row.value(2).toLongLong();
    row.finish();
    QSet<QString> expected;
    for (const auto &entry : request["text_array"].toArray()) expected.insert(entry.toObject()["msg_uuid"].toString());
    require(ids == expected, "ACK batch mismatch");
    if (success) {
        require(response["to_uid"].toInt() == request["to_uid"].toInt(), "ACK recipient mismatch");
        for (auto it = acknowledgements.begin(); it != acknowledgements.end(); ++it) {
            auto existing = query(_db, "SELECT message_id FROM messages WHERE sender_id=? AND client_uuid=? AND chat_id=?",
                {_uid, it.key(), chat});
            require(existing.next() && (existing.value(0).isNull() || existing.value(0).toLongLong() == it.value()),
                "ACK identity conflict");
            query(_db, "UPDATE messages SET message_id=?,state=? WHERE sender_id=? AND client_uuid=? AND chat_id=?",
                {it.value(), StoredMessage::Confirmed, _uid, it.key(), chat});
        }
        clearCommittedBatches();
    } else if (response["attempt_id"].toString() == QString::number(attempt)) {
        const auto error = response["commit_error"].toString();
        const bool terminal = error == "Conflict" || error == "InvalidUuid" || error == "InvalidMembership"
            || error == "UnauthorizedSender";
        query(_db, "UPDATE outgoing_batches SET paused=?,in_flight=CASE WHEN ?=1 THEN 0 ELSE in_flight END "
            "WHERE batch_key=?", {terminal ? 1 : 0, terminal ? 1 : 0, key});
        for (const auto &uuid : ids) query(_db, "UPDATE messages SET state=? WHERE sender_id=? AND client_uuid=? "
            "AND message_id IS NULL", {terminal && attempt == 1 ? StoredMessage::Failed : StoredMessage::Uncertain, _uid, uuid});
    }
    transaction.commit();
}

LocalMessageStore::~LocalMessageStore() { close(); }

void LocalMessageStore::open(const QString &accountRoot, int uid)
{
    close();
    _uid = uid;
    require(!accountRoot.isEmpty() && QDir().mkpath(accountRoot), "Cannot create message directory");
    _lock = std::make_unique<QLockFile>(QDir(accountRoot).filePath("messages.lock"));
    _lock->setStaleLockTime(0);
    require(_lock->tryLock(0), "This account's message database is already open");
    _connection = "messages-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    _db = QSqlDatabase::addDatabase("QSQLITE", _connection);
    _db.setDatabaseName(QDir(accountRoot).filePath("messages.sqlite"));
    _db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=3000");
    try {
        require(_db.open(), "Cannot open local message database");
        query(_db, "PRAGMA journal_mode=WAL");
        query(_db, "PRAGMA synchronous=FULL");
        auto version = query(_db, "PRAGMA user_version");
        require(version.next(), "Cannot read database version");
        const int schema = version.value(0).toInt();
        version.finish();
        require(schema >= 0 && schema <= 6, "Message database requires a newer client");
        if (schema > 0 && schema < 6) {
            auto backup = QDir(accountRoot).filePath(QString("messages.schema%1.sqlite").arg(schema));
            // A previous failed upgrade may leave an incomplete or stale backup. Never overwrite it.
            if (QFile::exists(backup)) backup = QDir(accountRoot).filePath(QString("messages.schema%1.").arg(schema)
                + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".sqlite");
            query(_db, "VACUUM INTO ?", {backup});
        }
        Transaction transaction(_db);
        if (schema == 0) {
            query(_db, "CREATE TABLE messages(local_id INTEGER PRIMARY KEY, message_id INTEGER, "
                "client_uuid TEXT,chat_id INTEGER NOT NULL,sender_id INTEGER NOT NULL,recipient_id INTEGER NOT NULL,"
                "content TEXT NOT NULL,sent_at INTEGER NOT NULL,state INTEGER NOT NULL,server_copy INTEGER NOT NULL DEFAULT 0)");
            query(_db, "CREATE UNIQUE INDEX message_server_id ON messages(chat_id,message_id) WHERE message_id IS NOT NULL");
            query(_db, "CREATE UNIQUE INDEX message_client_id ON messages(sender_id,client_uuid) WHERE client_uuid IS NOT NULL");
            query(_db, "CREATE TABLE sync_state(chat_id INTEGER PRIMARY KEY,cursor INTEGER NOT NULL)");
            query(_db, "PRAGMA user_version=1");
        }
        if (schema < 2) {
            query(_db, "CREATE TABLE outgoing_batches(batch_key TEXT PRIMARY KEY,chat_id INTEGER NOT NULL,"
                "payload BLOB NOT NULL,attempt INTEGER NOT NULL DEFAULT 0,retries INTEGER NOT NULL DEFAULT 0,"
                "due INTEGER NOT NULL DEFAULT 0,in_flight INTEGER NOT NULL DEFAULT 0,paused INTEGER NOT NULL DEFAULT 0)");
            query(_db, "CREATE TABLE outgoing_items(uuid TEXT PRIMARY KEY,batch_key TEXT NOT NULL)");
            query(_db, "CREATE TABLE message_receipts(chat_id INTEGER NOT NULL,message_id INTEGER NOT NULL,"
                "recipient_uid INTEGER NOT NULL,level INTEGER NOT NULL,revision INTEGER NOT NULL,"
                "PRIMARY KEY(chat_id,message_id,recipient_uid))");
            query(_db, "CREATE TABLE receipt_outbox(chat_id INTEGER NOT NULL,message_id INTEGER NOT NULL,"
                "level INTEGER NOT NULL,rejected INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(chat_id,message_id))");
            query(_db, "CREATE TABLE receipt_sync_state(chat_id INTEGER PRIMARY KEY,revision INTEGER NOT NULL)");
            query(_db, "PRAGMA user_version=2");
        }
        if (schema < 3) {
            query(_db, "CREATE TABLE contacts(id INTEGER PRIMARY KEY,data TEXT NOT NULL)");
            query(_db, "CREATE TABLE conversations(id INTEGER PRIMARY KEY,data TEXT NOT NULL)");
            query(_db, "CREATE TABLE applications(id INTEGER PRIMARY KEY,data TEXT NOT NULL)");
            query(_db, "PRAGMA user_version=3");
        }
        if (schema < 4) query(_db, "PRAGMA user_version=4");
        if (schema < 5) {
            query(_db, "CREATE TABLE conversation_attention(chat_id INTEGER PRIMARY KEY,seen_local_id INTEGER NOT NULL)");
            // 旧版本没有保存查看状态；不把所有历史消息重新当成新消息。
            if (schema > 0) query(_db, "INSERT INTO conversation_attention SELECT chat_id,MAX(local_id) FROM messages GROUP BY chat_id");
        }
        if (schema < 6) query(_db, "PRAGMA user_version=6");
        query(_db, "UPDATE outgoing_batches SET in_flight=0 WHERE in_flight=1");
        query(_db, "UPDATE messages SET state=? WHERE state=?", {StoredMessage::Uncertain, StoredMessage::Pending});
        transaction.commit();
    } catch (...) {
        close();
        throw;
    }
}

QVector<ConversationAttention> LocalMessageStore::conversationAttention(int chatId)
{
    require(chatId >= 0, "Invalid conversation identity");
    auto rows = query(_db,
        "SELECT m.chat_id,SUM(CASE WHEN m.sender_id<>? AND m.message_id IS NOT NULL "
        "AND m.local_id>COALESCE(a.seen_local_id,0) THEN 1 ELSE 0 END) "
        "FROM messages m LEFT JOIN conversation_attention a ON a.chat_id=m.chat_id "
        "WHERE (?=0 OR m.chat_id=?) GROUP BY m.chat_id", {_uid, chatId, chatId});
    QVector<ConversationAttention> result;
    while (rows.next()) result.append({rows.value(0).toInt(), rows.value(1).toLongLong()});
    if (chatId > 0 && result.isEmpty()) result.append({chatId, 0});
    return result;
}

void LocalMessageStore::markConversationSeen(int chatId, qint64 throughLocalId)
{
    require(chatId > 0 && throughLocalId >= 0, "Invalid conversation observation");
    query(_db, "INSERT INTO conversation_attention(chat_id,seen_local_id) "
        "SELECT ?,COALESCE(MAX(local_id),0) FROM messages WHERE chat_id=? AND local_id<=? "
        "ON CONFLICT(chat_id) DO UPDATE SET seen_local_id=MAX(seen_local_id,excluded.seen_local_id)",
        {chatId, chatId, throughLocalId});
}

void LocalMessageStore::close()
{
    if (!_connection.isEmpty()) {
        _db.close();
        _db = QSqlDatabase();
        QSqlDatabase::removeDatabase(_connection);
        _connection.clear();
    }
    _lock.reset();
}

qint64 LocalMessageStore::cursor(int chatId)
{
    auto rows = query(_db, "SELECT cursor FROM sync_state WHERE chat_id=?", {chatId});
    return rows.next() ? rows.value(0).toLongLong() : 0;
}

void LocalMessageStore::mergeServerMessage(const StoredMessage &message)
{
    auto receipt = query(_db, "SELECT recipient_uid FROM message_receipts WHERE chat_id=? AND message_id=?",
        {message.chatId, message.messageId});
    if (receipt.next()) require(receipt.value(0).toInt() == message.recipientId, "Receipt message identity conflict");
    receipt.finish();
    qint64 localId = 0;
    if (!message.clientMessageId.isEmpty()) {
        auto pending = query(_db, "SELECT local_id,message_id,chat_id,recipient_id,content FROM messages WHERE sender_id=? AND client_uuid=?",
            {message.senderId, message.clientMessageId});
        if (pending.next()) {
            require(pending.value(2).toInt() == message.chatId
                && pending.value(3).toInt() == message.recipientId
                && (pending.value(1).isNull() || pending.value(1).toLongLong() == message.messageId)
                && sameMessageContent(pending.value(4).toString(), message.content),
                "Server message UUID identity conflict");
            localId = pending.value(0).toLongLong();
        }
    }
    if (localId > 0) {
        // Preserve the local identity when history arrived before its ACK.
        query(_db, "DELETE FROM messages WHERE chat_id=? AND message_id=? AND local_id<>?",
            {message.chatId, message.messageId, localId});
        query(_db, "UPDATE messages SET message_id=?,content=?,sent_at=?,state=?,server_copy=1 "
            "WHERE local_id=? AND server_copy=0", {message.messageId, message.content, message.sentAt,
                StoredMessage::Confirmed, localId});
        return;
    }
    query(_db, "INSERT INTO messages(message_id,client_uuid,chat_id,sender_id,recipient_id,content,sent_at,state,server_copy) "
        "VALUES(?,?,?,?,?,?,?,?,1) ON CONFLICT(chat_id,message_id) WHERE message_id IS NOT NULL DO UPDATE SET "
        "client_uuid=excluded.client_uuid,content=excluded.content,sent_at=excluded.sent_at,state=excluded.state,server_copy=1 "
        "WHERE messages.server_copy=0",
        {message.messageId, message.clientMessageId.isEmpty() ? QVariant() : QVariant(message.clientMessageId),
            message.chatId, message.senderId, message.recipientId, message.content, message.sentAt, StoredMessage::Confirmed});
}

void LocalMessageStore::applySyncPage(int chatId, qint64 previous, qint64 next,
                                    const QVector<StoredMessage> &messages, const QString &epoch)
{
    Transaction transaction(_db);
    if (isGroupChat(chatId)) {
        const auto group = groupState(chatId);
        require(group["group_state"] == "active" && !epoch.isEmpty() && epoch == group["membership_epoch"].toString(), "Stale group page");
        for (const auto &message : messages) require(message.messageId > group["joined_after_id"].toInteger(), "Message predates membership");
    }
    require(previous == cursor(chatId) && next >= previous, "Stale synchronization page");
    qint64 last = previous;
    for (const auto &message : messages) {
        require(message.chatId == chatId && message.messageId > last && message.messageId <= next
            && message.senderId > 0 && (message.recipientId > 0 || (message.recipientId == 0 && isGroupChat(chatId))) && message.sentAt > 0,
            "Invalid synchronization page");
        mergeServerMessage(message);
        last = message.messageId;
    }
    require(last == next, "Synchronization cursor does not match the page");
    query(_db, "INSERT INTO sync_state(chat_id,cursor) VALUES(?,?) "
        "ON CONFLICT(chat_id) DO UPDATE SET cursor=excluded.cursor", {chatId, next});
    queueDelivered(chatId);
    clearCommittedBatches();
    transaction.commit();
}

void LocalMessageStore::saveOutgoing(const QVector<StoredMessage> &messages)
{
    Transaction transaction(_db);
    saveOutgoingRows(messages);
    transaction.commit();
}

void LocalMessageStore::saveOutgoingRows(const QVector<StoredMessage> &messages)
{
    for (const auto &message : messages) {
        require(message.chatId > 0 && message.senderId > 0
            && (message.recipientId > 0 || (message.recipientId == 0 && isGroupChat(message.chatId)))
            && !message.clientMessageId.isEmpty(), "Invalid outgoing message");
        auto prior = query(_db, "SELECT chat_id,recipient_id,content FROM messages WHERE sender_id=? AND client_uuid=?",
            {message.senderId, message.clientMessageId});
        if (prior.next()) {
            require(prior.value(0).toInt() == message.chatId && prior.value(1).toInt() == message.recipientId
                && prior.value(2).toString() == message.content, "Outgoing message UUID conflict");
        }
        prior.finish();
        query(_db, "INSERT INTO messages(client_uuid,chat_id,sender_id,recipient_id,content,sent_at,state) "
            "VALUES(?,?,?,?,?,?,?) ON CONFLICT(sender_id,client_uuid) WHERE client_uuid IS NOT NULL "
            "DO UPDATE SET state=excluded.state WHERE messages.message_id IS NULL",
            {message.clientMessageId, message.chatId, message.senderId, message.recipientId,
                message.content, message.sentAt, StoredMessage::Pending});
    }
}

void LocalMessageStore::acknowledge(int chatId, int senderId, const QString &uuid, qint64 messageId)
{
    require(messageId > 0 && !uuid.isEmpty(), "Invalid message acknowledgement");
    Transaction transaction(_db);
    auto pending = query(_db, "SELECT local_id FROM messages WHERE chat_id=? AND sender_id=? AND client_uuid=?",
        {chatId, senderId, uuid});
    if (pending.next()) {
        const qint64 localId = pending.value(0).toLongLong();
        pending.finish();
        auto existing = query(_db, "SELECT " + columns + " FROM messages WHERE chat_id=? AND message_id=? AND local_id<>?",
            {chatId, messageId, localId});
        if (existing.next()) {
            auto canonical = readMessage(existing);
            existing.finish();
            canonical.clientMessageId = uuid;
            mergeServerMessage(canonical);
        } else {
            existing.finish();
            query(_db, "UPDATE messages SET message_id=?,state=? WHERE local_id=?",
                {messageId, StoredMessage::Confirmed, localId});
        }
    }
    clearCommittedBatches();
    transaction.commit();
}

void LocalMessageStore::markUncertain(int chatId, const QVector<QString> &uuids)
{
    Transaction transaction(_db);
    for (const auto &uuid : uuids) {
        query(_db, "UPDATE messages SET state=? WHERE chat_id=? AND client_uuid=? AND message_id IS NULL",
            {StoredMessage::Uncertain, chatId, uuid});
    }
    transaction.commit();
}

LocalMessagePage LocalMessageStore::history(int chatId, qint64 before, int limit, qint64 from)
{
    require(limit > 0 && limit <= 200 && from >= 0 && (from == 0 || before == 0), "Invalid local history page size");
    LocalMessagePage page;
    auto rows = query(_db, "SELECT " + columns + " FROM messages WHERE chat_id=? AND message_id IS NOT NULL "
        "AND (?=0 OR message_id<?) AND (?=0 OR message_id>=?) ORDER BY message_id DESC LIMIT ?",
        {chatId, before, before, from, from, from > 0 ? -1 : limit + 1});
    while (rows.next()) page.messages.push_back(readMessage(rows));
    page.hasMore = from == 0 && page.messages.size() > limit;
    if (page.hasMore) page.messages.removeLast();
    if (from > 0) {
        auto older = query(_db, "SELECT 1 FROM messages WHERE chat_id=? AND message_id<? LIMIT 1", {chatId, from});
        page.hasMore = older.next();
    }
    std::reverse(page.messages.begin(), page.messages.end());
    if (before == 0) {
        auto pending = query(_db, "SELECT " + columns + " FROM messages WHERE chat_id=? AND message_id IS NULL "
            "ORDER BY local_id", {chatId});
        while (pending.next()) page.messages.push_back(readMessage(pending));
    }
    for (auto &message : page.messages) {
        auto receipt = query(_db, "SELECT level FROM message_receipts WHERE chat_id=? AND message_id=? AND recipient_uid=?",
            {chatId, message.messageId, message.recipientId});
        if (receipt.next()) message.receipt = static_cast<ReceiptLevel>(receipt.value(0).toInt());
    }
    return page;
}

QJsonArray LocalMessageStore::conversationSummaries()
{
    auto rows = query(_db, "SELECT c.id,m.content,m.sent_at FROM conversations c LEFT JOIN messages m "
        "ON m.local_id=(SELECT local_id FROM messages WHERE chat_id=c.id ORDER BY sent_at DESC,local_id DESC LIMIT 1) "
        "ORDER BY m.sent_at DESC,m.local_id DESC,c.id DESC");
    QJsonArray result;
    while (rows.next()) {
        auto text = rows.value(1).toString();
        if (text.startsWith("@resource:v1:"))
            text = QJsonDocument::fromJson(text.mid(13).toUtf8()).object()["name"].toString();
        result.append(QJsonObject{{"chat_id",rows.value(0).toInt()},{"summary",text},
            {"sent_at",QString::number(rows.value(2).toLongLong())}});
    }
    return result;
}

QJsonArray LocalMessageStore::directoryPage(const QString &kind, int after, int limit)
{
    require(kind == "contacts" || kind == "conversations" || kind == "applications", "Invalid directory kind");
    require(after >= 0 && limit > 0, "Invalid directory page");
    auto rows = query(_db, "SELECT data FROM " + kind + " WHERE id>? ORDER BY id LIMIT ?", {after, limit});
    QJsonArray result;
    while (rows.next()) result.append(QJsonDocument::fromJson(rows.value(0).toByteArray()).object());
    return result;
}

QJsonObject LocalMessageStore::directory()
{
    QJsonObject result;
    for (const auto &kind : {QString("contacts"), QString("conversations"), QString("applications")})
        result[kind] = directoryPage(kind, 0, INT_MAX);
    return result;
}

QJsonObject LocalMessageStore::mergeDirectory(const QJsonObject &directory)
{
    Transaction transaction(_db);
    QJsonObject changed;
    for (const auto &kind : {QString("contacts"), QString("conversations"), QString("applications")}) {
        QJsonArray saved;
        for (const auto &value : directory[kind].toArray()) {
            auto row = value.toObject();
            const int id = row["id"].toInt();
            require(id > 0, "Invalid directory identity");
            if (kind == "conversations" || kind == "contacts" || kind == "applications") {
                auto previous = query(_db, "SELECT data FROM " + kind + " WHERE id=?", {id});
                if (previous.next()) {
                    auto merged = QJsonDocument::fromJson(previous.value(0).toByteArray()).object();
                    if (kind == "conversations" && merged["type"] == "group" && merged.contains("group_revision")
                        && row["group_revision"].toString().toLongLong() < merged["group_revision"].toString().toLongLong()) continue;
                    for (const auto &version : {QString("profile_revision"),QString("relationship_revision"),QString("application_revision")}) {
                        if (!merged.contains(version) || row[version].toString().toLongLong() >= merged[version].toString().toLongLong()) continue;
                        const QStringList fields = version == "profile_revision"
                            ? (kind == "applications" ? QStringList{"name","icon","sex","profile_revision","applyname","applyicon","applysex","applydescription"}
                                : QStringList{"name","description","icon","sex","profile_revision"})
                            : version == "relationship_revision"
                            ? QStringList{"relationship_active","relationship_revision"}
                            : QStringList{"status","description","application_revision"};
                        for (const auto &field : fields) row.remove(field);
                    }
                    for (auto field = row.begin(); field != row.end(); ++field) merged[field.key()] = field.value();
                    row = merged;
                }
            }
            query(_db, "INSERT INTO " + kind + "(id,data) VALUES(?,?) ON CONFLICT(id) DO UPDATE SET data=excluded.data",
                {id, QJsonDocument(row).toJson(QJsonDocument::Compact)});
            if (kind == "conversations") {
                auto batches=query(_db,"SELECT batch_key,payload FROM outgoing_batches WHERE chat_id=?",{id});
                QStringList invalid;
                while (batches.next()) {
                    const auto request = QJsonDocument::fromJson(batches.value(1).toByteArray()).object();
                    if (!canSendGroup(request) || !canSendPrivate(request)) invalid.append(batches.value(0).toString());
                }
                batches.finish();
                for (const auto &key:invalid) {
                    query(_db,"UPDATE outgoing_batches SET paused=1,in_flight=0 WHERE batch_key=?",{key});
                    query(_db,"UPDATE messages SET state=? WHERE message_id IS NULL AND state=? AND client_uuid IN "
                        "(SELECT uuid FROM outgoing_items WHERE batch_key=?)",{StoredMessage::Uncertain,StoredMessage::Pending,key});
                }
            }
            auto stored = query(_db, "SELECT data FROM " + kind + " WHERE id=?", {id});
            require(stored.next(), "Missing stored directory row");
            saved.append(QJsonDocument::fromJson(stored.value(0).toByteArray()).object());
        }
        changed[kind] = saved;
    }
    const int approved = directory["approved_uid"].toInt();
    if (approved > 0) {
        auto stored = query(_db, "SELECT data FROM applications WHERE id=?", {approved});
        if (stored.next()) {
            auto application = QJsonDocument::fromJson(stored.value(0).toByteArray()).object();
            stored.finish();
            application["status"] = 1;
            query(_db, "UPDATE applications SET data=? WHERE id=?",
                {QJsonDocument(application).toJson(QJsonDocument::Compact), approved});
            auto rows = changed["applications"].toArray();
            rows.append(application);
            changed["applications"] = rows;
        }
    }
    transaction.commit();
    return changed;
}

bool LocalMessageStore::isGroupChat(int chatId)
{
    auto row = query(_db, "SELECT data FROM conversations WHERE id=?", {chatId});
    return row.next() && QJsonDocument::fromJson(row.value(0).toByteArray()).object()["type"].toString() == "group";
}

QJsonArray LocalMessageStore::findDirectory(const QString &kind, const QString &text, int after, int limit)
{
    require((kind == "contacts" || kind == "conversations") && text.size() <= 200 && after >= 0
        && limit > 0 && limit <= 100, "Invalid directory search");
    auto rows = query(_db,"SELECT id,data FROM " + kind + " WHERE id>? ORDER BY id",{after});
    QJsonArray result;
    while (rows.next() && result.size() < limit) {
        const auto row = QJsonDocument::fromJson(rows.value(1).toByteArray()).object();
        if (kind == "conversations" && row["type"] != "group") continue;
        if (kind == "contacts" && (row["is_self"].toBool() || (row.contains("relationship_active") && !row["relationship_active"].toBool()))) continue;
        if (row["name"].toString().contains(text,Qt::CaseInsensitive)
            || row["backname"].toString().contains(text,Qt::CaseInsensitive)
            || QString::number(rows.value(0).toInt()).contains(text)) result.append(row);
    }
    return result;
}

QJsonObject LocalMessageStore::groupState(int chatId)
{
    auto row = query(_db, "SELECT data FROM conversations WHERE id=?", {chatId});
    if (!row.next()) return {};
    const auto value = QJsonDocument::fromJson(row.value(0).toByteArray()).object();
    return value["type"] == "group" ? value : QJsonObject{};
}

bool LocalMessageStore::canSendGroup(const QJsonObject &request)
{
    const auto group = groupState(request["chat_id"].toInt());
    if (group.isEmpty()) return request["chat_type"] != "group";
    return group["group_state"] == "active" && !request["membership_epoch"].toString().isEmpty()
        && request["membership_epoch"] == group["membership_epoch"];
}

QVector<StoredMessage> LocalMessageStore::search(int chatId, const QString &text, qint64 before, int limit)
{
    require(chatId > 0 && !text.trimmed().isEmpty() && text.size() <= 200 && limit > 0 && limit <= 100, "Invalid search");
    auto rows = query(_db, "SELECT " + columns + " FROM messages WHERE chat_id=? AND (?=0 OR local_id<?) "
        "ORDER BY local_id DESC", {chatId,before,before});
    QVector<StoredMessage> result;
    while (rows.next() && result.size()<limit) {
        const auto message=readMessage(rows);
        const auto visible=message.content.startsWith("@resource:v1:")
            ? QJsonDocument::fromJson(message.content.mid(13).toUtf8()).object()["name"].toString() : message.content;
        if (visible.contains(text,Qt::CaseInsensitive)) result.push_back(message);
    }
    return result;
}

bool LocalMessageStore::canSendPrivate(const QJsonObject &request)
{
    if (request["chat_type"] == "group") return true;
    auto stored = query(_db,"SELECT data FROM conversations WHERE id=?",{request["chat_id"].toInt()});
    if (!stored.next()) return true;
    const auto state = QJsonDocument::fromJson(stored.value(0).toByteArray()).object();
    if (!state.contains("relationship_revision")) return true;
    const auto version = request["relationship_revision"].toString("1");
    return state["relationship_active"].toBool() && version == state["relationship_revision"].toString();
}
