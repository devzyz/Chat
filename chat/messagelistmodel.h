#ifndef MESSAGELISTMODEL_H
#define MESSAGELISTMODEL_H

#include "messagerecord.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

/** @brief 在 GUI 线程拥有一个会话的消息值及身份索引，合并服务器状态且不允许状态倒退。 */
class MessageListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        MessageIdRole = Qt::UserRole + 1,
        ClientMessageIdRole,
        ChatIdRole,
        SenderIdRole,
        SenderNameRole,
        AvatarKeyRole,
        AvatarRole,
        SentAtRole,
        DeliveryStatusRole,
        IsSelfRole,
        MessageTypeRole,
        TextRole,
        ResourceIdRole,
        LocalResourcePathRole,
        DurableRole,
        ReadConfirmedRole,
        ResourcePreviewRole
    };
    Q_ENUM(Role)

    /** @brief 初始化对象，用于在 GUI 线程拥有一个会话的消息值及身份索引，合并服务器状态且不允许状态倒退。 */
    explicit MessageListModel(int chatId);

    /** @brief 返回根索引下消息数量，子索引不包含消息行。 */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** @brief 按角色读取有效索引的数据；无效索引或未知角色返回空 QVariant。 */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /** @brief 返回模型公开角色与名称的对应表。 */
    QHash<int, QByteArray> roleNames() const override;

    /** @brief 返回此模型绑定的会话 ID。 */
    int chatId() const;
    /** @brief 按资源 ID 更新本地文件路径及预览，并通知受影响消息行。 */
    void setResourceFile(const QString& resourceId, const QString& path, const QPixmap& preview);
    /** @brief 返回模型内部记录的借用指针；越界返回 nullptr。任何可能修改消息容器的调用或模型销毁后必须重新获取。 */
    const MessageRecord *recordAt(int row) const;

    /** @brief 追加一条当前会话的非重复消息；成功返回 1，重复或会话不匹配返回 0。 */
    int appendMessage(const MessageRecord &message);
    /** @brief 批量追加当前会话的消息并返回新增数量，跳过已有或批内重复身份。 */
    int appendMessages(const QVector<MessageRecord> &messages);
    /** @brief 将未重复的历史记录插入现有列表之前，返回新增数量。 */
    int prependHistory(const QVector<MessageRecord> &messages);
    /** @brief 合并消息身份、内容与状态，并维护排序和模型变更通知。 */
    void mergeMessages(const QVector<MessageRecord> &messages);
    /** @brief 替换指定发送者的头像并通知受影响的消息行。 */
    void updateSenderAvatar(int senderId, const QPixmap &avatar);

    /** @brief 按客户端 UUID 及可选发送者匹配 ACK，写入服务器 ID 并单调推进状态；未找到返回 false。 */
    bool acknowledgeMessage(const QString &clientMessageId, qint64 messageId,
                            DeliveryStatus status = DeliveryStatus::Sent, int senderId = -1);
    /** @brief 按 UUID 和可选发送者更新状态；不倒退已确认的送达或已读事实。 */
    bool updateStatusByClientId(const QString &clientMessageId, DeliveryStatus status, int senderId = -1);
    /** @brief 按服务器 ID 更新消息状态，未命中返回 false。 */
    bool updateStatusByMessageId(qint64 messageId, DeliveryStatus status);
    /** @brief 删除服务器 ID 对应消息并重建索引，未命中返回 false。 */
    bool removeByMessageId(qint64 messageId);

    /** @brief 按客户端 UUID 和可选发送者查行，找不到时返回 -1。 */
    int rowForClientMessageId(const QString &clientMessageId, int senderId = -1) const;
    /** @brief 按服务器消息 ID 查行，找不到时返回 -1。 */
    int rowForMessageId(qint64 messageId) const;
    /** @brief 按服务器 ID 或客户端 UUID 恢复索引，找不到时返回无效 QModelIndex。 */
    QModelIndex indexForStableId(qint64 messageId, const QString &clientMessageId) const;
    /** @brief 返回已加载的最早有效服务器消息 ID，无有效记录时返回 0。 */
    qint64 oldestMessageId() const;

    /** @brief 查询历史是否仍可能存在下一页。 */
    bool canLoadMore() const;
    /** @brief 更新历史分页的后续页标志。 */
    void setCanLoadMore(bool canLoadMore);
    /** @brief 查询当前是否有历史加载操作等待结束。 */
    bool isLoadingHistory() const;
    /** @brief 设置历史加载标志以抑制重复请求。 */
    void setLoadingHistory(bool loading);
    /** @brief 查询初始历史页是否已经处理。 */
    bool hasLoadedInitialPage() const;
    /** @brief 记录初始历史页是否处理完成。 */
    void setInitialPageLoaded(bool loaded);
    /** @brief 返回当前历史分页游标。 */
    qint64 historyCursor() const;
    /** @brief 保存下一次历史查询所用游标。 */
    void setHistoryCursor(qint64 cursor);

private:
    /** @brief 断言调用发生在 GUI 线程，防止跨线程修改模型。 */
    void assertGuiThread() const;
    /** @brief 检查服务器 ID 或客户端身份是否已经存在。 */
    bool contains(const MessageRecord &message) const;
    /** @brief 根据当前消息顺序重建服务器 ID 及客户端 UUID 行索引。 */
    void rebuildRowIndexes();
    /** @brief 对有效行应用单调状态合并并通知变化，无法更新时返回 false。 */
    bool updateStatusAtRow(int row, DeliveryStatus status);

    int _chatId;
    QVector<MessageRecord> _messages;
    QHash<QString, QHash<int, int>> _clientIdRows;
    QHash<qint64, int> _messageIdRows;
    bool _canLoadMore = true;
    bool _loadingHistory = false;
    bool _initialPageLoaded = false;
    qint64 _historyCursor = 0;
};

#endif // MESSAGELISTMODEL_H
