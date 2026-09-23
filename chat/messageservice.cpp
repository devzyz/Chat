#include "messageservice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <stdexcept>

/** @brief 在专用 Qt 工作线程拥有 LocalMessageStore，关闭和销毁也在该线程执行。 */
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
    _outgoingTimer.setInterval(1000);
    connect(&_outgoingTimer, &QTimer::timeout, this, &MessageService::dispatchOutgoing);
    _syncTimer.setInterval(30000);
    connect(&_syncTimer, &QTimer::timeout, this,
        /** @brief 定时为已登记会话同步消息并上报及拉取回执。 */
        [this] {
        for (int chat : _chats) { synchronize(chat); synchronizeReceipts(chat); sendReceipts(chat); }
    });
}

MessageService::~MessageService()
{
    stop();
    // Quit is queued behind accepted writes and close; no disk work runs on the GUI thread.
    QMetaObject::invokeMethod(_worker,
        /** @brief 在已接受的写入及关闭操作之后退出工作线程事件循环。 */
        [] { QThread::currentThread()->quit(); }, Qt::QueuedConnection);
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
    QMetaObject::invokeMethod(_worker,
        /** @brief 在 SQLite 线程执行存储操作并收集错误，再排队返回对象线程。 */
        [this, generation, chatId, operation = std::move(operation),
                                       completion = std::move(completion), failure = std::move(failure)] {
        QString error;
        try { operation(_worker->store); }
        catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); }
        QMetaObject::invokeMethod(this,
            /** @brief 减少在途计数，仅对当前账号代调用成功或失败回调。 */
            [this, generation, chatId, error, completion, failure] {
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

void MessageService::start(const QString &accountRoot, int uid, bool receipts)
{
    stop();
    if (uid <= 0 || accountRoot.isEmpty()) return;
    _uid = uid;
    _accountRoot = accountRoot;
    _receipts = receipts;
    auto recovering = std::make_shared<QSet<int>>();
    execute(0,
        /** @brief 打开账号库、读取待对账会话并恢复发送状态。 */
        [accountRoot, uid, recovering](LocalMessageStore &store) {
        store.open(accountRoot, uid);
        *recovering = store.recoveryChats();
        store.resumeOutgoing();
    },
        /** @brief 登记恢复中的会话并启动发送调度及恢复期限。 */
        [this, recovering] {
        _recoveringChats = *recovering;
        for (int chat : *recovering) registerChat(chat);
        const auto generation = _generation;
        QTimer::singleShot(15000, this,
            /** @brief 恢复期限到达后仅对当前账号释放发送等待。 */
            [this, generation] {
            if (generation != _generation) return;
            _recoveringChats.clear();
            dispatchOutgoing();
        });
        _outgoingTimer.start();
        dispatchOutgoing();
    });
    _syncTimer.start();
}

void MessageService::stop()
{
    ++_generation;
    _uid = 0;
    _accountRoot.clear();
    _failedDrafts.clear();
    _syncTimer.stop();
    _outgoingTimer.stop();
    _dispatching = false;
    _receipts = false;
    _receiptRequests.clear(); _receiptSync.clear(); _receiptReport.clear();
    _receiptCommitting.clear(); _receiptRetries.clear(); _receiptSingles.clear();
    _receiptWaiting.clear(); _receiptQueued.clear();
    _chats.clear();
    _recoveringChats.clear();
    _requests.clear();
    _committing.clear();
    QMetaObject::invokeMethod(_worker,
        /** @brief 在存储队列中关闭上一个账号连接。 */
        [worker = _worker] { worker->store.close(); }, Qt::QueuedConnection);
}

void MessageService::registerChat(int chatId)
{
    if (!isActive() || chatId <= 0) return;
    if (!_chats.contains(chatId)) {
        _chats.insert(chatId);
        synchronize(chatId);
        synchronizeReceipts(chatId);
        sendReceipts(chatId);
    }
}

void MessageService::synchronize(int chatId)
{
    if (!isActive() || !_chats.contains(chatId) || _requests.contains(chatId)) return;
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _requests.insert(chatId, requestId);
    auto cursor = std::make_shared<qint64>(0);
    execute(chatId,
        /** @brief 在存储线程读取已提交的消息游标。 */
        [chatId, cursor](LocalMessageStore &store) { *cursor = store.cursor(chatId); },
        /** @brief 游标读取完成后发送仍匹配当前请求的增量查询。 */
        [this, chatId, requestId, cursor] {
            if (_requests.value(chatId) != requestId) return;
            emit syncRequested(QJsonObject{{"mode", "sync_v1"}, {"uid", _uid}, {"chat_id", chatId},
                {"after_id", *cursor}, {"request_id", requestId}});
            const auto generation = _generation;
            QTimer::singleShot(15000, this,
                /** @brief 仅为仍未完成且未提交的请求报告同步超时。 */
                [this, generation, chatId, requestId] {
                if (generation != _generation || _requests.value(chatId) != requestId || _committing.contains(chatId)) return;
                _requests.remove(chatId);
                emit failed(chatId, tr("消息同步超时，将稍后重试"));
            });
        });
}

void MessageService::acceptSyncPage(const QJsonObject &response)
{
    const int chatId = response["chat_id"].toInt();
    const QString requestId = response["request_id"].toString();
    if (!isActive() || requestId.isEmpty() || _requests.value(chatId) != requestId || _committing.contains(chatId)) return;
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
    execute(chatId,
        /** @brief 事务合并服务器消息页与同步游标。 */
        [chatId, previous, next, messages](LocalMessageStore &store) {
        store.applySyncPage(chatId, previous, next, messages);
    },
        /** @brief 提交后清除请求标记，通知消息变化并安排下一页或发送恢复。 */
        [this, chatId, more, next, changed = !messages.isEmpty()] {
        _committing.remove(chatId);
        _requests.remove(chatId);
        sendReceipts(chatId);
        if (more) synchronize(chatId);
        else {
            _recoveringChats.remove(chatId);
            dispatchOutgoing();
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
    execute(chatId,
        /** @brief 确保当前账号库已打开后原子保存发送请求。 */
        [request, root = _accountRoot, uid = _uid](LocalMessageStore &store) {
        if (!store.isOpen()) store.open(root, uid);
        store.saveOutgoingRequest(request);
    },
        /** @brief 持久化成功后删除草稿错误并启动发送调度。 */
        [this, chatId, uuids] {
        for (const auto &uuid : uuids) _failedDrafts.remove(uuid);
        _outgoingTimer.start();
        emit messagesChanged(chatId);
        dispatchOutgoing();
    },
        /** @brief 持久化失败时保留草稿供用户重试并通知发送失败。 */
        [this, chatId, uuids, request] {
        for (const auto &uuid : uuids) _failedDrafts.insert(uuid, request);
        emit sendFailed(chatId, uuids);
    });
}

void MessageService::acknowledge(int chatId, const QString &uuid, qint64 messageId)
{
    if (!isActive()) return;
    execute(chatId,
        /** @brief 在存储线程合并指定 UUID 的服务器确认 ID。 */
        [chatId, uuid, messageId, senderId = _uid](LocalMessageStore &store) {
        store.acknowledge(chatId, senderId, uuid, messageId);
    },
        /** @brief ACK 落盘后刷新模型并发起消息对账。 */
        [this, chatId] { emit messagesChanged(chatId); synchronize(chatId); });
}

void MessageService::markUncertain(int chatId, const QVector<QString> &uuids)
{
    if (!isActive()) return;
    execute(chatId,
        /** @brief 在存储线程标记指定消息发送结果不确定。 */
        [chatId, uuids](LocalMessageStore &store) { store.markUncertain(chatId, uuids); },
        /** @brief 不确定状态落盘后刷新模型并启动同步。 */
        [this, chatId] { emit messagesChanged(chatId); synchronize(chatId); });
}

void MessageService::loadHistory(int chatId, qint64 before, qint64 from)
{
    if (!isActive()) { emit failed(chatId, tr("尚未建立消息存储会话")); return; }
    auto page = std::make_shared<LocalMessagePage>();
    execute(chatId,
        /** @brief 在存储线程读取指定游标之前的一页历史。 */
        [chatId, before, from, page](LocalMessageStore &store) { *page = store.history(chatId, before, 50, from); },
        /** @brief 在当前账号线程发布历史消息值及后续页标志。 */
        [this, chatId, before, page] { emit historyLoaded(chatId, before, page->messages, page->hasMore); });
}

void MessageService::dispatchOutgoing()
{
    const int waiting = qMin(qsizetype(8), _receiptWaiting.size());
    for (int index = 0; index < waiting; ++index) {
        const auto work = _receiptWaiting.dequeue();
        _receiptQueued.remove(qint64(work.first) * 2 + work.second);
        requestReceipts(work.first, work.second);
    }
    if (!isActive() || _dispatching) return;
    _dispatching = true;
    auto requests = std::make_shared<QVector<QJsonObject>>();
    auto changed = std::make_shared<QSet<int>>();
    execute(-1,
        /** @brief 持久化到期批次的发送 attempt 并收集变更会话。 */
        [requests, changed, recovering = _recoveringChats](LocalMessageStore &store) {
        *requests = store.dispatchDue(QDateTime::currentMSecsSinceEpoch(), recovering, changed.get());
    },
        /** @brief 解除调度占用，刷新变更会话并发送已落盘批次。 */
        [this, requests, changed] {
            _dispatching = false;
            for (int chat : *changed) emit messagesChanged(chat);
            for (const auto &request : *requests) {
                emit sendRequested(request);
            }
        },
            /** @brief 存储调度失败后解除发送调度占用。 */
            [this] { _dispatching = false; });
}

void MessageService::pauseOutgoing()
{
    _outgoingTimer.stop();
    if (isActive()) execute(0,
        /** @brief 在存储线程暂停未完成发送批次。 */
        [](LocalMessageStore &store) { store.pauseOutgoing(); });
}

void MessageService::retry(int chatId, const QString &uuid)
{
    if (!isActive()) return;
    if (_failedDrafts.contains(uuid) && _failedDrafts.value(uuid)["chat_id"].toInt() == chatId) {
        send(_failedDrafts.value(uuid));
        return;
    }
    execute(chatId,
        /** @brief 在存储线程为原 UUID 安排用户重试。 */
        [uuid](LocalMessageStore &store) { store.retry(uuid); },
        /** @brief 重试状态落盘后立即检查到期批次。 */
        [this] { dispatchOutgoing(); });
}

void MessageService::acceptSendResponse(const QJsonObject &response)
{
    if (!isActive()) return;
    const int chatId = response["chat_id"].toInt();
    execute(chatId,
        /** @brief 将发送响应匹配到本地持久化批次。 */
        [response](LocalMessageStore &store) { store.acceptSendResponse(response); },
        /** @brief ACK 应用后通知模型及网络层，并启动会话同步。 */
        [this, chatId, response] {
            emit messagesChanged(chatId);
            emit sendResponseApplied(response);
            registerChat(chatId);
            synchronize(chatId);
        });
}

void MessageService::observeRead(int chatId, const QVector<qint64> &ids)
{
    if (!isActive() || !_receipts || !_chats.contains(chatId) || ids.isEmpty()) return;
    execute(chatId,
        /** @brief 在存储线程记录已满足可见性的已读意图。 */
        [chatId, ids](LocalMessageStore &store) { store.observeRead(chatId, ids); },
        /** @brief 已读意图落盘后请求上报回执。 */
        [this, chatId] { sendReceipts(chatId); });
}

void MessageService::synchronizeReceipts(int chatId) { requestReceipts(chatId, false); }
void MessageService::sendReceipts(int chatId) { requestReceipts(chatId, true); }

void MessageService::receiptRetry(int chatId)
{
    const int retry = _receiptRetries.value(chatId);
    _receiptRetries[chatId] = retry + 1;
    if (retry >= 3) return; // The periodic synchronization resumes persistent work.
    const int delays[] = {1000, 3000, 10000};
    const auto generation = _generation;
    QTimer::singleShot(delays[retry], this,
        /** @brief 重试定时器到期时仅继续当前账号回执。 */
        [this, chatId, generation] {
        if (generation == _generation) sendReceipts(chatId);
    });
}

void MessageService::requestReceipts(int chatId, bool report)
{
    if (!isActive() || !_receipts || !_chats.contains(chatId)) return;
    auto &busy = report ? _receiptReport : _receiptSync;
    if (busy.contains(chatId)) return;
    if (busy.size() >= 8) {
        const qint64 key = qint64(chatId) * 2 + report;
        if (!_receiptQueued.contains(key)) {
            _receiptQueued.insert(key);
            _receiptWaiting.enqueue({chatId, report});
        }
        return;
    }
    busy.insert(chatId);
    auto request = std::make_shared<QJsonObject>(QJsonObject{{"version", 1}, {"chat_id", chatId},
        {"request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
    execute(chatId,
        /** @brief 在存储线程读取待报回执或 revision 游标，支持单条降级上报。 */
        [chatId, report, request, single = _receiptSingles.contains(chatId)](LocalMessageStore &store) {
        if (report) {
            auto items = store.pendingReceipts(chatId);
            if (single && !items.isEmpty()) items = QJsonArray{items.first()};
            (*request)["items"] = items;
        } else (*request)["after_revision"] = QString::number(store.receiptCursor(chatId));
    },
        /** @brief 登记回执请求并发送，空上报批次直接释放占用。 */
        [this, chatId, report, request] {
        if (report && (*request)["items"].toArray().isEmpty()) { _receiptReport.remove(chatId); return; }
        const auto requestId = (*request)["request_id"].toString();
        _receiptRequests.insert(requestId, *request);
        emit receiptRequested(report ? 1029 : 1032, *request);
        const auto generation = _generation;
        QTimer::singleShot(15000, this,
            /** @brief 回执超时仅处理当前账号的未提交请求并安排重试。 */
            [this, requestId, generation, chatId, report] {
            if (generation != _generation || !_receiptRequests.contains(requestId) || _receiptCommitting.contains(requestId)) return;
            _receiptRequests.remove(requestId);
            (report ? _receiptReport : _receiptSync).remove(chatId);
            if (report) receiptRetry(chatId);
        });
    },
        /** @brief 存储读取失败后释放对应回执通道。 */
        [this, chatId, report] { (report ? _receiptReport : _receiptSync).remove(chatId); });
}

void MessageService::acceptReceiptResponse(const QJsonObject &response)
{
    const auto requestId = response["request_id"].toString();
    if (!isActive() || !_receipts || !_receiptRequests.contains(requestId) || _receiptCommitting.contains(requestId)) return;
    const auto request = _receiptRequests.value(requestId);
    const int chatId = request["chat_id"].toInt();
    const bool report = request.contains("items");
    if (response["chat_id"].toInt() != chatId || response["version"].toInt() != 1) return;
    if (response["error"].toInt(-1) != 0) {
        _receiptRequests.remove(requestId);
        (report ? _receiptReport : _receiptSync).remove(chatId);
        const auto error = response["receipt_error"].toString();
        if (report && (error == "InvalidMessage" || error == "InvalidRequest" || error == "Unauthorized")) {
            _receiptSingles.insert(chatId);
            const auto items = request["items"].toArray();
            if (items.size() == 1) {
                execute(chatId,
                    /** @brief 删除已被服务器拒绝且无需继续重试的单条回执。 */
                    [chatId, id = items.first().toObject()["message_id"].toInteger()](LocalMessageStore &store) {
                    store.discardReceipt(chatId, id);
                });
                emit failed(chatId, tr("服务器拒绝了消息回执"));
            }
        }
        if (report) receiptRetry(chatId);
        return;
    }
    if (!response["items"].isArray()) return;
    const auto items = response["items"].toArray();
    qint64 previous = -1, next = -1;
    if (report) {
        QMap<qint64, QString> expected;
        for (const auto &item : request["items"].toArray())
            expected.insert(item.toObject()["message_id"].toInteger(), item.toObject()["level"].toString());
        if (expected.size() != items.size()) return;
        for (const auto &value : items) {
            const auto item = value.toObject();
            const auto id = item["message_id"].toInteger();
            if (!expected.contains(id) || item["recipient_uid"].toInt() != _uid
                || (expected.value(id) == "read" && item["level"].toString() != "read")) return;
            expected.remove(id);
        }
    } else {
        bool valid = false;
        previous = request["after_revision"].toString().toLongLong();
        next = response["next_revision"].toString().toLongLong(&valid);
        if (!valid || next < previous || QString::number(next) != response["next_revision"].toString()
            || response["after_revision"] != request["after_revision"] || !response["load_more"].isBool()
            || (response["load_more"].toBool() && next == previous)) return;
    }
    _receiptCommitting.insert(requestId);
    execute(chatId,
        /** @brief 在事务中合并回执事实并推进 revision 游标。 */
        [chatId, items, previous, next](LocalMessageStore &store) {
        store.acceptReceipts(chatId, items, previous, next);
    },
        /** @brief 回执落盘后释放关联状态，刷新消息并继续后续页。 */
        [this, chatId, requestId, report, more = response["load_more"].toBool()] {
        _receiptCommitting.remove(requestId); _receiptRequests.remove(requestId);
        (report ? _receiptReport : _receiptSync).remove(chatId);
        _receiptRetries.remove(chatId);
        emit messagesChanged(chatId);
        if (report) sendReceipts(chatId);
        else if (more) synchronizeReceipts(chatId);
    },
        /** @brief 回执提交失败后清理占用并按上报策略重试。 */
        [this, requestId, chatId, report] {
        _receiptCommitting.remove(requestId); _receiptRequests.remove(requestId);
        (report ? _receiptReport : _receiptSync).remove(chatId);
        if (report) receiptRetry(chatId);
    });
}
