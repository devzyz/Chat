#include "messagemodelstore.h"

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
