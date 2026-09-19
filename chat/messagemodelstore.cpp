#include "messagemodelstore.h"

bool MessageModelStore::applyHistory(int chatId, const QVector<MessageRecord> &records,
                                   bool canLoadMore, qint64 nextCursor)
{
    auto *model = getOrCreate(chatId);
    qint64 previousId = 0;
    for (const auto &record : records) {
        if (record.chatId != chatId || record.messageId <= previousId) {
            model->setLoadingHistory(false);
            return false;
        }
        previousId = record.messageId;
    }
    if (nextCursor < 0 || (!records.isEmpty() && nextCursor != previousId) ||
        (records.isEmpty() && canLoadMore)) {
        model->setLoadingHistory(false);
        return false;
    }
    const bool advances = !model->hasLoadedInitialPage() || nextCursor > model->historyCursor();
    model->prependHistory(records);
    // Overlapping/replayed pages must not rewind the cursor or reopen an exhausted scan.
    if (advances || (nextCursor == model->historyCursor() && !canLoadMore)) {
        model->setCanLoadMore(canLoadMore);
        model->setHistoryCursor(nextCursor);
    }
    model->setInitialPageLoaded(true);
    model->setLoadingHistory(false);
    return true;
}

void MessageModelStore::acknowledge(int chatId, const QVector<MessageAcknowledgement> &acknowledgements, int senderId)
{
    if (auto *model = find(chatId)) {
        for (const auto &ack : acknowledgements)
            model->acknowledgeMessage(ack.clientMessageId, ack.messageId, DeliveryStatus::Sent, senderId);
    }
}

void MessageModelStore::markFailed(int chatId, const QVector<QString> &clientIds, int senderId)
{
    if (auto *model = find(chatId)) {
        for (const auto &id : clientIds) model->updateStatusByClientId(id, DeliveryStatus::Failed, senderId);
    }
}

void MessageModelStore::updateSenderAvatar(int senderId, const QPixmap &avatar)
{
    for (auto &entry : _models) {
        entry.second->updateSenderAvatar(senderId, avatar);
    }
}

MessageListModel *MessageModelStore::getOrCreate(int chatId)
{
    const auto found = _models.find(chatId);
    if (found != _models.end()) {
        return found->second.get();
    }

    auto model = std::make_unique<MessageListModel>(chatId);
    auto *result = model.get();
    _models.emplace(chatId, std::move(model));
    return result;
}

MessageListModel *MessageModelStore::find(int chatId) const
{
    const auto found = _models.find(chatId);
    return found == _models.end() ? nullptr : found->second.get();
}
