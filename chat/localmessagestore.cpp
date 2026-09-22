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
void require(bool success, const char *operation)
{
    if (!success) throw std::runtime_error(operation);
}

bool sameMessageContent(const QString &original, const QString &canonical)
{
    if (original == canonical) return true;
    if (!original.startsWith("@resource:v1:") || !canonical.startsWith("@resource:v1:")) return false;
    const auto left = QJsonDocument::fromJson(original.mid(13).toUtf8());
    const auto right = QJsonDocument::fromJson(canonical.mid(13).toUtf8());
    return left.isObject() && right.isObject() && !left.object()["resource_id"].toString().isEmpty()
        && left.object() == right.object();
}

QSqlQuery query(QSqlDatabase &db, const QString &sql, const QVariantList &values = {})
{
    QSqlQuery result(db);
    require(result.prepare(sql), "Cannot prepare local message query");
    for (const auto &value : values) result.addBindValue(value);
    require(result.exec(), "Local message database operation failed");
    return result;
}

class Transaction {
public:
    explicit Transaction(QSqlDatabase &db) : _db(db) { require(_db.transaction(), "Cannot start transaction"); }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;
    ~Transaction() {
        if (!_committed && !_db.rollback()) {
            // Never reuse a connection whose transaction outcome is unknown.
            qWarning("Local message rollback failed; closing connection");
            _db.close();
        }
    }
    void commit() { require(_db.commit(), "Cannot commit local messages"); _committed = true; }
private:
    QSqlDatabase &_db;
    bool _committed = false;
};

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
    require(chat > 0 && sender == _uid && recipient > 0 && !items.isEmpty() && bytes.size() <= 1950,
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
        require(schema >= 0 && schema <= 2, "Message database requires a newer client");
        if (schema == 1) {
            auto backup = QDir(accountRoot).filePath("messages.schema1.sqlite");
            // A previous failed upgrade may leave an incomplete or stale backup. Never overwrite it.
            if (QFile::exists(backup)) backup = QDir(accountRoot).filePath("messages.schema1."
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
        query(_db, "UPDATE outgoing_batches SET in_flight=0 WHERE in_flight=1");
        query(_db, "UPDATE messages SET state=? WHERE state=?", {StoredMessage::Uncertain, StoredMessage::Pending});
        transaction.commit();
    } catch (...) {
        close();
        throw;
    }
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
                                    const QVector<StoredMessage> &messages)
{
    Transaction transaction(_db);
    require(previous == cursor(chatId) && next >= previous, "Stale synchronization page");
    qint64 last = previous;
    for (const auto &message : messages) {
        require(message.chatId == chatId && message.messageId > last && message.messageId <= next
            && message.senderId > 0 && message.recipientId > 0 && message.sentAt > 0,
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
        require(message.chatId > 0 && message.senderId > 0 && message.recipientId > 0
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
