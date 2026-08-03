#ifndef MESSAGEMODELSTORE_H
#define MESSAGEMODELSTORE_H

#include "messagelistmodel.h"

#include <memory>
#include <unordered_map>

class MessageModelStore
{
public:
    MessageListModel *getOrCreate(int chatId);
    MessageListModel *find(int chatId) const;

private:
    std::unordered_map<int, std::unique_ptr<MessageListModel>> _models;
};

#endif // MESSAGEMODELSTORE_H
