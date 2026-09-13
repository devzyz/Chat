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
    bool applyHistory(int chatId, const QVector<MessageRecord> &records, bool canLoadMore, qint64 nextCursor);
    void acknowledge(int chatId, const QVector<MessageAcknowledgement> &acknowledgements);
    void markFailed(int chatId, const QVector<QString> &clientIds);

private:
    std::unordered_map<int, std::unique_ptr<MessageListModel>> _models;
};

#endif // MESSAGEMODELSTORE_H
