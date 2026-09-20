#include "messageservice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <stdexcept>

class MessageStorageWorker final : public QObject {
public:
    LocalMessageStore store;
};

MessageService::MessageService(QObject *parent) : QObject(parent), _worker(new MessageStorageWorker)
{
    qRegisterMetaType<QVector<StoredMessage>>();
    _worker->moveToThread(&_thread);
    connect(&_thread, &QThread::finished, _worker, &QObject::deleteLater);
    _thread.start();
    _syncTimer.setInterval(30000);
    connect(&_syncTimer, &QTimer::timeout, this, [this] {
        for (int chat : _chats) synchronize(chat);
    });
}

MessageService::~MessageService()
{
    stop();
    // Quit is queued behind accepted writes and close; no disk work runs on the GUI thread.
    QMetaObject::invokeMethod(_worker, [] { QThread::currentThread()->quit(); }, Qt::QueuedConnection);
    _thread.wait();
}

void MessageService::execute(int chatId, std::function<void(LocalMessageStore &)> operation,
                             std::function<void()> completion, std::function<void()> failure)
{
    const auto generation = _generation;
    // Bound accepted disk work, including completions waiting for the GUI loop.
    // Account reset drains accepted writes; overload rejects before any network send.
    // Account-open control work must follow close even when old-session work fills the queue.
    if (chatId != 0 && _pendingOperations >= 256) {
        _requests.remove(chatId);
        _committing.remove(chatId);
        emit failed(chatId, tr("本地消息处理繁忙，请稍后重试"));
        if (failure) failure();
        return;
    }
    ++_pendingOperations;
    QMetaObject::invokeMethod(_worker, [this, generation, chatId, operation = std::move(operation),
                                       completion = std::move(completion), failure = std::move(failure)] {
        QString error;
        try { operation(_worker->store); }
        catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); }
        QMetaObject::invokeMethod(this, [this, generation, chatId, error, completion, failure] {
            --_pendingOperations;
            if (generation != _generation || !isActive()) return;
            if (!error.isEmpty()) {
                _requests.remove(chatId);
                _committing.remove(chatId);
                emit failed(chatId, error);
                if (failure) failure();
            } else if (completion) completion();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MessageService::start(const QString &accountRoot, int uid)
{
    stop();
    if (uid <= 0 || accountRoot.isEmpty()) return;
    _uid = uid;
    execute(0, [accountRoot](LocalMessageStore &store) { store.open(accountRoot); });
    _syncTimer.start();
}

void MessageService::stop()
{
    ++_generation;
    _uid = 0;
    _syncTimer.stop();
    _chats.clear();
    _requests.clear();
    _committing.clear();
    QMetaObject::invokeMethod(_worker, [worker = _worker] { worker->store.close(); }, Qt::QueuedConnection);
}

void MessageService::registerChat(int chatId)
{
    if (!isActive() || chatId <= 0) return;
    if (!_chats.contains(chatId)) {
        _chats.insert(chatId);
        synchronize(chatId);
    }
}

void MessageService::synchronize(int chatId)
{
    if (!isActive() || !_chats.contains(chatId) || _requests.contains(chatId)) return;
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _requests.insert(chatId, token);
    auto cursor = std::make_shared<qint64>(0);
    execute(chatId, [chatId, cursor](LocalMessageStore &store) { *cursor = store.cursor(chatId); },
        [this, chatId, token, cursor] {
            if (_requests.value(chatId) != token) return;
            emit syncRequested(QJsonObject{{"mode", "sync_v1"}, {"uid", _uid}, {"chat_id", chatId},
                {"after_id", *cursor}, {"request_id", token}});
            const auto generation = _generation;
            QTimer::singleShot(15000, this, [this, generation, chatId, token] {
                if (generation != _generation || _requests.value(chatId) != token || _committing.contains(chatId)) return;
                _requests.remove(chatId);
                emit failed(chatId, tr("消息同步超时，将稍后重试"));
            });
        });
}

void MessageService::acceptSyncPage(const QJsonObject &response)
{
    const int chatId = response["chat_id"].toInt();
    const QString token = response["request_id"].toString();
    if (!isActive() || token.isEmpty() || _requests.value(chatId) != token || _committing.contains(chatId)) return;
    if (response["mode"].toString() != "sync_v1" || !response.contains("error")
        || response["error"].toInt(-1) != 0 || !response["msgs"].isArray()
        || !response["after_id"].isDouble() || !response["next_cursor"].isDouble()
        || !response["load_more"].isBool()) {
        _requests.remove(chatId);
        emit failed(chatId, tr("服务器未能完成消息同步"));
        return;
    }
    QVector<StoredMessage> messages;
    for (const auto &value : response["msgs"].toArray()) {
        const auto row = value.toObject();
        StoredMessage message;
        message.chatId = chatId;
        message.messageId = row["message_id"].toInteger();
        message.clientMessageId = row["msg_uuid"].toString();
        message.senderId = row["send_id"].toInt();
        message.recipientId = row["recv_id"].toInt();
        message.content = row["content"].toString();
        message.sentAt = row["created_at"].toInteger() * 1000;
        message.state = StoredMessage::Confirmed;
        messages.push_back(message);
    }
    const qint64 previous = response["after_id"].toInteger();
    const qint64 next = response["next_cursor"].toInteger();
    const bool more = response["load_more"].toBool();
    if (more && next <= previous) {
        _requests.remove(chatId);
        emit failed(chatId, tr("服务器消息同步游标未前进"));
        return;
    }
    _committing.insert(chatId);
    execute(chatId, [chatId, previous, next, messages](LocalMessageStore &store) {
        store.applySyncPage(chatId, previous, next, messages);
    }, [this, chatId, more, next, changed = !messages.isEmpty()] {
        _committing.remove(chatId);
        _requests.remove(chatId);
        if (more) synchronize(chatId);
        else {
            if (changed) emit messagesChanged(chatId);
            emit synchronized(chatId, next);
        }
    });
}

void MessageService::send(const QJsonObject &request)
{
    const int chatId = request["chat_id"].toInt();
    if (!isActive() || request["from_uid"].toInt() != _uid || chatId <= 0) {
        emit failed(chatId, tr("尚未建立消息存储会话"));
        return;
    }
    QVector<StoredMessage> messages;
    for (const auto &value : request["text_array"].toArray()) {
        const auto entry = value.toObject();
        StoredMessage message;
        message.chatId = chatId;
        message.senderId = _uid;
        message.recipientId = request["to_uid"].toInt();
        message.clientMessageId = entry["msg_uuid"].toString();
        message.content = entry["msg_content"].toString();
        message.sentAt = QDateTime::currentMSecsSinceEpoch();
        messages.push_back(message);
    }
    if (messages.isEmpty()) return;
    QVector<QString> uuids;
    for (const auto &message : messages) uuids.push_back(message.clientMessageId);
    execute(chatId, [messages](LocalMessageStore &store) { store.saveOutgoing(messages); },
        [this, chatId, request, uuids] {
            emit messagesChanged(chatId);
            emit sendRequested(request);
            const auto generation = _generation;
            QTimer::singleShot(15000, this, [this, generation, chatId, uuids] {
                if (generation == _generation && isActive()) markUncertain(chatId, uuids);
            });
        }, [this, chatId, uuids] { emit sendFailed(chatId, uuids); });
}

void MessageService::acknowledge(int chatId, const QString &uuid, qint64 messageId)
{
    if (!isActive()) return;
    execute(chatId, [chatId, uuid, messageId, senderId = _uid](LocalMessageStore &store) {
        store.acknowledge(chatId, senderId, uuid, messageId);
    }, [this, chatId] { emit messagesChanged(chatId); synchronize(chatId); });
}

void MessageService::markUncertain(int chatId, const QVector<QString> &uuids)
{
    if (!isActive()) return;
    execute(chatId, [chatId, uuids](LocalMessageStore &store) { store.markUncertain(chatId, uuids); },
        [this, chatId] { emit messagesChanged(chatId); synchronize(chatId); });
}

void MessageService::loadHistory(int chatId, qint64 before, qint64 from)
{
    if (!isActive()) { emit failed(chatId, tr("尚未建立消息存储会话")); return; }
    auto page = std::make_shared<LocalMessagePage>();
    execute(chatId, [chatId, before, from, page](LocalMessageStore &store) { *page = store.history(chatId, before, 50, from); },
        [this, chatId, before, page] { emit historyLoaded(chatId, before, page->messages, page->hasMore); });
}
