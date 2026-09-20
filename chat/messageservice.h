#pragma once

#include "localmessagestore.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <functional>

class MessageStorageWorker;

// Account-scoped coordinator. Network requests/results cross this interface;
// SQLite work stays on one worker, and callbacks are discarded after reset.
class MessageService final : public QObject {
    Q_OBJECT
public:
    explicit MessageService(QObject *parent = nullptr);
    ~MessageService() override;
    void start(const QString &accountRoot, int uid);
    void stop();
    bool isActive() const { return _uid > 0; }
    void registerChat(int chatId);
    void synchronize(int chatId);
    void acceptSyncPage(const QJsonObject &response);
    void send(const QJsonObject &request);
    void acknowledge(int chatId, const QString &uuid, qint64 messageId);
    void markUncertain(int chatId, const QVector<QString> &uuids);
    void loadHistory(int chatId, qint64 before = 0, qint64 from = 0);
signals:
    void syncRequested(QJsonObject request);
    void sendRequested(QJsonObject request);
    void messagesChanged(int chatId);
    void synchronized(int chatId, qint64 cursor);
    void historyLoaded(int chatId, qint64 before, QVector<StoredMessage> messages, bool hasMore);
    void sendFailed(int chatId, QVector<QString> uuids);
    void failed(int chatId, QString reason);
private:
    void execute(int chatId, std::function<void(LocalMessageStore &)> operation,
                 std::function<void()> completion = {}, std::function<void()> failure = {});
    int _uid = 0;
    quint64 _generation = 0;
    int _pendingOperations = 0;
    QSet<int> _chats;
    QHash<int, QString> _requests;
    QSet<int> _committing;
    QThread _thread;
    MessageStorageWorker *_worker;
    QTimer _syncTimer;
};
