#ifndef MESSAGEMODELSTORE_H
#define MESSAGEMODELSTORE_H

#include "messagelistmodel.h"

#include <memory>
#include <unordered_map>

/** @brief 在 GUI 线程按会话独占拥有消息模型；借出指针只在存储及该模型存活期间有效。 */
class MessageModelStore
{
public:
    /** @brief 返回会话模型的借用指针，缺失时创建；指针不可跨存储销毁或账号重置保存。 */
    MessageListModel *getOrCreate(int chatId);
    /** @brief 返回已有会话模型的借用指针，未找到时返回 nullptr；不创建模型。 */
    MessageListModel *find(int chatId) const;
    /** @brief 向会话模型合并历史记录并更新游标和分页标志。 */
    bool applyHistory(int chatId, const QVector<MessageRecord> &records, bool canLoadMore, qint64 nextCursor);
    /** @brief 将 ACK 对应的服务器 ID 和状态应用到指定会话的本地消息。 */
    void acknowledge(int chatId, const QVector<MessageAcknowledgement> &acknowledgements, int senderId = -1);
    /** @brief 将指定客户端 UUID 对应的待发送消息标记为失败。 */
    void markFailed(int chatId, const QVector<QString> &clientIds, int senderId = -1);
    /** @brief 替换指定发送者的头像并通知受影响的消息行。 */
    void updateSenderAvatar(int senderId, const QPixmap &avatar);

private:
    std::unordered_map<int, std::unique_ptr<MessageListModel>> _models;
};

#endif // MESSAGEMODELSTORE_H
