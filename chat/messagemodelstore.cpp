#include "messagemodelstore.h"

#include <algorithm>

bool MessageModelStore::applyHistory(int chatId, const QVector<MessageRecord> &records,
                                   bool canLoadMore, qint64 nextCursor)
{
    auto *model = getOrCreate(chatId);
    const auto oldest = model->oldestMessageId();
    if (model->hasLoadedInitialPage() && oldest > 0 &&
        std::any_of(records.cbegin(), records.cend(), [model, oldest](const MessageRecord &record) {
            return record.messageId >= oldest && model->rowForMessageId(record.messageId) < 0;
        })) {
        model->setCanLoadMore(false);
        model->setLoadingHistory(false);
        return false;
    }
    model->prependHistory(records);
    model->setCanLoadMore(canLoadMore);
    model->setHistoryCursor(nextCursor > 0 ? nextCursor : model->oldestMessageId());
    model->setInitialPageLoaded(true);
    model->setLoadingHistory(false);
    return true;
}

void MessageModelStore::acknowledge(int chatId, const QVector<MessageAcknowledgement> &acknowledgements)
{
    if (auto *model = find(chatId)) {
        for (const auto &ack : acknowledgements)
            model->acknowledgeMessage(ack.clientMessageId, ack.messageId, DeliveryStatus::Sent);
    }
}

void MessageModelStore::markFailed(int chatId, const QVector<QString> &clientIds)
{
    if (auto *model = find(chatId)) {
        for (const auto &id : clientIds) model->updateStatusByClientId(id, DeliveryStatus::Failed);
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
