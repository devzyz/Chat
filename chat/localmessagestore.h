#pragma once

#include <QJsonObject>
#include <QLockFile>
#include <QMetaType>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <memory>

// Persistence values contain no widgets, pixmaps or presentation text.
struct StoredMessage {
    enum State { Pending, Confirmed, Uncertain, Failed };
    qint64 localId = 0;
    qint64 messageId = 0;
    QString clientMessageId;
    int chatId = 0;
    int senderId = 0;
    int recipientId = 0;
    QString content;
    qint64 sentAt = 0;
    State state = Pending;
};
Q_DECLARE_METATYPE(StoredMessage)
Q_DECLARE_METATYPE(QVector<StoredMessage>)

struct LocalMessagePage {
    QVector<StoredMessage> messages;
    bool hasMore = false;
};

// All methods, including destruction, run on the connection's owning thread.
// Errors throw; a failed transaction never advances a synchronization cursor.
class LocalMessageStore final {
public:
    LocalMessageStore() = default;
    ~LocalMessageStore();
    LocalMessageStore(const LocalMessageStore &) = delete;
    LocalMessageStore &operator=(const LocalMessageStore &) = delete;
    void open(const QString &accountRoot);
    void close();
    qint64 cursor(int chatId);
    void applySyncPage(int chatId, qint64 previous, qint64 next, const QVector<StoredMessage> &messages);
    void saveOutgoing(const QVector<StoredMessage> &messages);
    void acknowledge(int chatId, int senderId, const QString &uuid, qint64 messageId);
    void markUncertain(int chatId, const QVector<QString> &uuids);
    LocalMessagePage history(int chatId, qint64 before, int limit, qint64 from = 0);
private:
    void mergeServerMessage(const StoredMessage &message);
    QSqlDatabase _db;
    QString _connection;
    std::unique_ptr<QLockFile> _lock;
};
