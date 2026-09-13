#include "messagelistmodel.h"

#include <QSet>
#include <QThread>
#include <algorithm>

namespace {
bool messageLess(const MessageRecord &left, const MessageRecord &right)
{
    if (left.messageId > 0 && right.messageId > 0 && left.messageId != right.messageId) {
        return left.messageId < right.messageId;
    }
    if (left.sentAt != right.sentAt) {
        return left.sentAt < right.sentAt;
    }
    return left.clientMessageId < right.clientMessageId;
}
}

MessageListModel::MessageListModel(int chatId)
    : QAbstractListModel(nullptr), _chatId(chatId)
{
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : _messages.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _messages.size()) {
        return {};
    }

    const auto &message = _messages.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TextRole: return message.text;
    case MessageIdRole: return message.messageId;
    case ClientMessageIdRole: return message.clientMessageId;
    case ChatIdRole: return message.chatId;
    case SenderIdRole: return message.senderId;
    case SenderNameRole: return message.senderName;
    case AvatarKeyRole: return message.avatarKey;
    case AvatarRole: return message.avatar;
    case SentAtRole: return message.sentAt;
    case DeliveryStatusRole: return static_cast<int>(message.deliveryStatus);
    case IsSelfRole: return message.isSelf;
    case MessageTypeRole: return static_cast<int>(message.messageType);
    default: return {};
    }
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        {MessageIdRole, "messageId"},
        {ClientMessageIdRole, "clientMessageId"},
        {ChatIdRole, "chatId"},
        {SenderIdRole, "senderId"},
        {SenderNameRole, "senderName"},
        {AvatarKeyRole, "avatarKey"},
        {AvatarRole, "avatar"},
        {SentAtRole, "sentAt"},
        {DeliveryStatusRole, "deliveryStatus"},
        {IsSelfRole, "isSelf"},
        {MessageTypeRole, "messageType"},
        {TextRole, "text"}
    };
}

int MessageListModel::chatId() const
{
    return _chatId;
}

const MessageRecord *MessageListModel::recordAt(int row) const
{
    return row >= 0 && row < _messages.size() ? &_messages.at(row) : nullptr;
}

int MessageListModel::appendMessage(const MessageRecord &message)
{
    return appendMessages({message});
}

int MessageListModel::appendMessages(const QVector<MessageRecord> &messages)
{
    assertGuiThread();
    QVector<MessageRecord> filtered;
    QSet<qint64> batchMessageIds;
    QSet<QString> batchClientIds;
    filtered.reserve(messages.size());

    for (const auto &message : messages) {
        if (message.chatId != _chatId || contains(message)) {
            continue;
        }
        if (message.messageId > 0 && batchMessageIds.contains(message.messageId)) {
            continue;
        }
        if (!message.clientMessageId.isEmpty() && batchClientIds.contains(message.clientMessageId)) {
            continue;
        }
        if (message.messageId > 0) batchMessageIds.insert(message.messageId);
        if (!message.clientMessageId.isEmpty()) batchClientIds.insert(message.clientMessageId);
        filtered.push_back(message);
    }

    if (filtered.isEmpty()) {
        return 0;
    }

    std::stable_sort(filtered.begin(), filtered.end(), messageLess);
    const int first = _messages.size();
    const int last = first + filtered.size() - 1;
    beginInsertRows({}, first, last);
    _messages += filtered;
    endInsertRows();
    rebuildRowIndexes();
    return filtered.size();
}

int MessageListModel::prependHistory(const QVector<MessageRecord> &messages)
{
    assertGuiThread();
    QVector<MessageRecord> filtered;
    QSet<qint64> batchMessageIds;
    QSet<QString> batchClientIds;
    filtered.reserve(messages.size());

    for (const auto &message : messages) {
        if (message.chatId != _chatId || contains(message)) {
            continue;
        }
        if (message.messageId > 0 && batchMessageIds.contains(message.messageId)) {
            continue;
        }
        if (!message.clientMessageId.isEmpty() && batchClientIds.contains(message.clientMessageId)) {
            continue;
        }
        if (message.messageId > 0) batchMessageIds.insert(message.messageId);
        if (!message.clientMessageId.isEmpty()) batchClientIds.insert(message.clientMessageId);
        filtered.push_back(message);
    }

    if (filtered.isEmpty()) {
        return 0;
    }

    std::stable_sort(filtered.begin(), filtered.end(), messageLess);
    beginInsertRows({}, 0, filtered.size() - 1);
    _messages = filtered + _messages;
    endInsertRows();
    rebuildRowIndexes();
    return filtered.size();
}

bool MessageListModel::acknowledgeMessage(const QString &clientMessageId, qint64 messageId,
                                          DeliveryStatus status)
{
    assertGuiThread();
    int row = rowForClientMessageId(clientMessageId);
    if (row < 0 || messageId <= 0) {
        return false;
    }

    const int duplicateRow = rowForMessageId(messageId);
    if (duplicateRow >= 0 && duplicateRow != row) {
        // A history/peer row can arrive before the pending send is acknowledged.
        // Keep the pending UUID identity but collapse the duplicate server ID.
        beginRemoveRows({}, duplicateRow, duplicateRow);
        _messages.removeAt(duplicateRow);
        endRemoveRows();
        rebuildRowIndexes();
        row = rowForClientMessageId(clientMessageId);
    }

    auto &message = _messages[row];
    if (message.messageId > 0) {
        _messageIdRows.remove(message.messageId);
    }
    message.messageId = messageId;
    message.deliveryStatus = status;
    if (messageId > 0) {
        _messageIdRows.insert(messageId, row);
    }
    const auto changed = index(row);
    emit dataChanged(changed, changed, {MessageIdRole, DeliveryStatusRole});
    return true;
}

bool MessageListModel::updateStatusByClientId(const QString &clientMessageId, DeliveryStatus status)
{
    assertGuiThread();
    return updateStatusAtRow(rowForClientMessageId(clientMessageId), status);
}

bool MessageListModel::updateStatusByMessageId(qint64 messageId, DeliveryStatus status)
{
    assertGuiThread();
    return updateStatusAtRow(rowForMessageId(messageId), status);
}

bool MessageListModel::removeByMessageId(qint64 messageId)
{
    assertGuiThread();
    const int row = rowForMessageId(messageId);
    if (row < 0) {
        return false;
    }
    beginRemoveRows({}, row, row);
    _messages.removeAt(row);
    endRemoveRows();
    rebuildRowIndexes();
    return true;
}

int MessageListModel::rowForClientMessageId(const QString &clientMessageId) const
{
    return clientMessageId.isEmpty() ? -1 : _clientIdRows.value(clientMessageId, -1);
}

int MessageListModel::rowForMessageId(qint64 messageId) const
{
    return messageId <= 0 ? -1 : _messageIdRows.value(messageId, -1);
}

QModelIndex MessageListModel::indexForStableId(qint64 messageId, const QString &clientMessageId) const
{
    int row = rowForMessageId(messageId);
    if (row < 0) {
        row = rowForClientMessageId(clientMessageId);
    }
    return row >= 0 ? index(row) : QModelIndex();
}

qint64 MessageListModel::oldestMessageId() const
{
    for (const auto &message : _messages) {
        if (message.messageId > 0) {
            return message.messageId;
        }
    }
    return 0;
}

bool MessageListModel::canLoadMore() const { return _canLoadMore; }
void MessageListModel::setCanLoadMore(bool canLoadMore) { assertGuiThread(); _canLoadMore = canLoadMore; }
bool MessageListModel::isLoadingHistory() const { return _loadingHistory; }
void MessageListModel::setLoadingHistory(bool loading) { assertGuiThread(); _loadingHistory = loading; }
bool MessageListModel::hasLoadedInitialPage() const { return _initialPageLoaded; }
void MessageListModel::setInitialPageLoaded(bool loaded) { assertGuiThread(); _initialPageLoaded = loaded; }
qint64 MessageListModel::historyCursor() const { return _historyCursor; }
void MessageListModel::setHistoryCursor(qint64 cursor) { assertGuiThread(); _historyCursor = cursor; }

void MessageListModel::assertGuiThread() const
{
    Q_ASSERT(QThread::currentThread() == thread());
}

bool MessageListModel::contains(const MessageRecord &message) const
{
    return (message.messageId > 0 && _messageIdRows.contains(message.messageId))
        || (!message.clientMessageId.isEmpty() && _clientIdRows.contains(message.clientMessageId));
}

void MessageListModel::rebuildRowIndexes()
{
    _clientIdRows.clear();
    _messageIdRows.clear();
    for (int row = 0; row < _messages.size(); ++row) {
        const auto &message = _messages.at(row);
        if (message.messageId > 0) _messageIdRows.insert(message.messageId, row);
        if (!message.clientMessageId.isEmpty()) _clientIdRows.insert(message.clientMessageId, row);
    }
}

bool MessageListModel::updateStatusAtRow(int row, DeliveryStatus status)
{
    if (row < 0 || row >= _messages.size() || _messages[row].deliveryStatus == status) {
        return false;
    }
    _messages[row].deliveryStatus = status;
    const auto changed = index(row);
    emit dataChanged(changed, changed, {DeliveryStatusRole});
    return true;
}
