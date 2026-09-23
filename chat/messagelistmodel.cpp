#include "messagelistmodel.h"

#include <QSet>
#include <QThread>
#include <algorithm>

namespace {
/** @brief 已确认消息优先按服务器 ID 排序，其余按发送时间及客户端 UUID 排序。 */
bool messageLess(const MessageRecord &left, const MessageRecord &right)
{
    if ((left.messageId > 0) != (right.messageId > 0)) return left.messageId > 0;
    if (left.messageId > 0 && right.messageId > 0 && left.messageId != right.messageId) {
        return left.messageId < right.messageId;
    }
    if (left.sentAt != right.sentAt) {
        return left.sentAt < right.sentAt;
    }
    return left.clientMessageId < right.clientMessageId;
}
}

void MessageListModel::mergeMessages(const QVector<MessageRecord> &messages)
{
    assertGuiThread();
    beginResetModel();
    for (const auto &message : messages) {
        if (message.chatId != _chatId) continue;
        int match = -1;
        for (int row = _messages.size() - 1; row >= 0; --row) {
            const auto &existing = _messages[row];
            if ((message.messageId > 0 && existing.messageId == message.messageId)
                || (!message.clientMessageId.isEmpty() && existing.clientMessageId == message.clientMessageId
                    && existing.senderId == message.senderId)) {
                if (match >= 0) _messages.removeAt(match);
                match = row;
            }
        }
        if (match < 0) _messages.push_back(message);
        else {
            auto updated = message;
            const auto &existing = _messages[match];
            updated.deliveryStatus = mergeDeliveryStatus(existing.deliveryStatus, updated.deliveryStatus);
            if (updated.clientMessageId.isEmpty()) updated.clientMessageId = existing.clientMessageId;
            if (updated.localResourcePath.isEmpty()) {
                updated.localResourcePath = existing.localResourcePath;
                updated.resourcePreview = existing.resourcePreview;
            }
            _messages[match] = std::move(updated);
        }
    }
    std::stable_sort(_messages.begin(), _messages.end(), messageLess);
    rebuildRowIndexes();
    endResetModel();
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
    case Qt::ToolTipRole:
        if (message.deliveryStatus == DeliveryStatus::Uncertain) return tr("发送结果待核实，可双击使用原消息编号重试");
        if (message.deliveryStatus == DeliveryStatus::Failed) return tr("消息未能发送");
        switch (message.deliveryStatus) {
        case DeliveryStatus::Queued: return tr("等待发送");
        case DeliveryStatus::Sending: return tr("正在发送");
        case DeliveryStatus::Sent: return tr("已发送至服务器，尚未确认对方收到");
        case DeliveryStatus::Delivered: return tr("已送达对方客户端，尚未确认阅读");
        case DeliveryStatus::Read: return tr("对方已读");
        default: return {};
        }
    case DurableRole: return message.durable;
    case ReadConfirmedRole: return message.readConfirmed;
    case Qt::AccessibleTextRole: return message.text + " " + data(index, Qt::ToolTipRole).toString();
    case Qt::DisplayRole:
    case TextRole: return message.text;
    case ResourceIdRole: return message.resourceId;
    case LocalResourcePathRole: return message.localResourcePath;
    case ResourcePreviewRole: return message.resourcePreview;
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

void MessageListModel::updateSenderAvatar(int senderId, const QPixmap &avatar)
{
    assertGuiThread();
    for (int row = 0; row < _messages.size(); ++row) {
        if (_messages[row].senderId == senderId) {
            _messages[row].avatar = avatar;
            emit dataChanged(index(row), index(row), {AvatarRole});
        }
    }
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
    QSet<QPair<int, QString>> batchClientIds;
    filtered.reserve(messages.size());

    for (const auto &message : messages) {
        if (message.chatId != _chatId || contains(message)) {
            continue;
        }
        if (message.messageId > 0 && batchMessageIds.contains(message.messageId)) {
            continue;
        }
        if (!message.clientMessageId.isEmpty() && batchClientIds.contains({message.senderId, message.clientMessageId})) {
            continue;
        }
        if (message.messageId > 0) batchMessageIds.insert(message.messageId);
        if (!message.clientMessageId.isEmpty()) batchClientIds.insert({message.senderId, message.clientMessageId});
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
    QSet<QPair<int, QString>> batchClientIds;
    filtered.reserve(messages.size());

    for (const auto &message : messages) {
        const int pendingRow = rowForClientMessageId(message.clientMessageId, message.senderId);
        if (message.chatId == _chatId && message.messageId > 0 && pendingRow >= 0 &&
            _messages[pendingRow].messageId == 0) {
            acknowledgeMessage(message.clientMessageId, message.messageId, message.deliveryStatus, message.senderId);
        }
        if (message.chatId != _chatId || contains(message)) {
            continue;
        }
        if (message.messageId > 0 && batchMessageIds.contains(message.messageId)) {
            continue;
        }
        if (!message.clientMessageId.isEmpty() && batchClientIds.contains({message.senderId, message.clientMessageId})) {
            continue;
        }
        if (message.messageId > 0) batchMessageIds.insert(message.messageId);
        if (!message.clientMessageId.isEmpty()) batchClientIds.insert({message.senderId, message.clientMessageId});
        filtered.push_back(message);
    }

    if (!filtered.isEmpty()) {
        const int first = _messages.size();
        beginInsertRows({}, first, first + filtered.size() - 1);
        _messages += filtered;
        endInsertRows();
        rebuildRowIndexes();
    }
    if (!std::is_sorted(_messages.cbegin(), _messages.cend(), messageLess)) {
        emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        const auto previousIndexes = persistentIndexList();
        QVector<int> order;
        for (int row = 0; row < _messages.size(); ++row) order.push_back(row);
        std::stable_sort(order.begin(), order.end(),
            /** @brief 按消息时间和稳定身份对合并行排序。 */
            [this](int left, int right) {
            return messageLess(_messages[left], _messages[right]);
        });
        QVector<MessageRecord> sorted;
        QVector<int> newRows(_messages.size());
        sorted.reserve(_messages.size());
        for (int row = 0; row < order.size(); ++row) {
            sorted.push_back(_messages[order[row]]);
            newRows[order[row]] = row;
        }
        _messages = std::move(sorted);
        rebuildRowIndexes();
        QModelIndexList updatedIndexes;
        for (const auto &previous : previousIndexes) updatedIndexes.push_back(index(newRows[previous.row()]));
        changePersistentIndexList(previousIndexes, updatedIndexes);
        emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    }
    return filtered.size();
}

bool MessageListModel::acknowledgeMessage(const QString &clientMessageId, qint64 messageId,
                                          DeliveryStatus status, int senderId)
{
    assertGuiThread();
    int row = rowForClientMessageId(clientMessageId, senderId);
    if (row < 0 || messageId <= 0
        || (_messages[row].messageId > 0 && _messages[row].messageId != messageId)) {
        return false;
    }

    const int duplicateRow = rowForMessageId(messageId);
    if (duplicateRow >= 0 && duplicateRow != row) {
        if (_messages[duplicateRow].senderId != _messages[row].senderId) return false;
        status = mergeDeliveryStatus(_messages[duplicateRow].deliveryStatus, status);
        _messages[row].durable |= _messages[duplicateRow].durable;
        _messages[row].readConfirmed |= _messages[duplicateRow].readConfirmed;
        // A history/peer row can arrive before the pending send is acknowledged.
        // Keep the pending UUID identity but collapse the duplicate server ID.
        beginRemoveRows({}, duplicateRow, duplicateRow);
        _messages.removeAt(duplicateRow);
        endRemoveRows();
        rebuildRowIndexes();
        row = rowForClientMessageId(clientMessageId, senderId);
    }

    auto &message = _messages[row];
    if (message.messageId > 0) {
        _messageIdRows.remove(message.messageId);
    }
    message.messageId = messageId;
    message.deliveryStatus = mergeDeliveryStatus(message.deliveryStatus, status);
    if (messageId > 0) {
        _messageIdRows.insert(messageId, row);
    }
    const auto changed = index(row);
    emit dataChanged(changed, changed, {MessageIdRole, DeliveryStatusRole});
    return true;
}

bool MessageListModel::updateStatusByClientId(const QString &clientMessageId, DeliveryStatus status, int senderId)
{
    assertGuiThread();
    return updateStatusAtRow(rowForClientMessageId(clientMessageId, senderId), status);
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

int MessageListModel::rowForClientMessageId(const QString &clientMessageId, int senderId) const
{
    const auto found = _clientIdRows.constFind(clientMessageId);
    if (clientMessageId.isEmpty() || found == _clientIdRows.cend()) return -1;
    if (senderId >= 0) return found->value(senderId, -1);
    // A UUID without its sender must never select an arbitrary account's row.
    return found->size() == 1 ? found->cbegin().value() : -1;
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
        || (rowForClientMessageId(message.clientMessageId, message.senderId) >= 0);
}

void MessageListModel::rebuildRowIndexes()
{
    _clientIdRows.clear();
    _messageIdRows.clear();
    for (int row = 0; row < _messages.size(); ++row) {
        const auto &message = _messages.at(row);
        if (message.messageId > 0) _messageIdRows.insert(message.messageId, row);
        if (!message.clientMessageId.isEmpty()) _clientIdRows[message.clientMessageId].insert(message.senderId, row);
    }
}

bool MessageListModel::updateStatusAtRow(int row, DeliveryStatus status)
{
    if (row < 0 || row >= _messages.size() || _messages[row].deliveryStatus == status) {
        return false;
    }
    status = mergeDeliveryStatus(_messages[row].deliveryStatus, status);
    if (_messages[row].deliveryStatus == status) return false;
    _messages[row].deliveryStatus = status;
    const auto changed = index(row);
    emit dataChanged(changed, changed, {DeliveryStatusRole});
    return true;
}

void MessageListModel::setResourceFile(const QString& resourceId, const QString& path, const QPixmap& preview)
{
    assertGuiThread();
    for (int row = 0; row < _messages.size(); ++row) {
        auto& message = _messages[row];
        if (message.resourceId != resourceId) continue;
        message.localResourcePath = path;
        message.resourcePreview = preview;
        emit dataChanged(index(row), index(row), {LocalResourcePathRole, ResourcePreviewRole});
    }
}
