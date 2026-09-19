#include "localmessagestore.h"

#include <QDir>
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
}

LocalMessageStore::~LocalMessageStore() { close(); }

void LocalMessageStore::open(const QString &accountRoot)
{
    close();
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
        require(schema >= 0 && schema <= 1, "Message database requires a newer client");
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
    qint64 localId = 0;
    if (!message.clientMessageId.isEmpty()) {
        auto pending = query(_db, "SELECT local_id,message_id,chat_id,recipient_id FROM messages WHERE sender_id=? AND client_uuid=?",
            {message.senderId, message.clientMessageId});
        if (pending.next()) {
            require(pending.value(2).toInt() == message.chatId
                && pending.value(3).toInt() == message.recipientId
                && (pending.value(1).isNull() || pending.value(1).toLongLong() == message.messageId),
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
    transaction.commit();
}

void LocalMessageStore::saveOutgoing(const QVector<StoredMessage> &messages)
{
    Transaction transaction(_db);
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
    transaction.commit();
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
    return page;
}
