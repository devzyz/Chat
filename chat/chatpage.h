#ifndef CHATPAGE_H
#define CHATPAGE_H

#include "messagemodelstore.h"
#include "messagerecord.h"
#include "userdata.h"
#include "localmessagestore.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QWidget>
#include <memory>

namespace Ui {
class ChatPage;
}

class MessageItemDelegate;
class ResourceTransferManager;

/** @brief 展示会话消息并协调历史加载、发送、已读观测及资源传输。 */
class ChatPage : public QWidget
{
    Q_OBJECT

public:
    /** @brief 创建消息视图并连接头像、存储和已读观测事件。 */
    explicit ChatPage(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ChatPage();

    /** @brief 切换当前会话并恢复其消息与滚动状态。 */
    void setChatInfo(std::shared_ptr<ChatInfo> chatInfo);
    /** @brief 将旧消息数据转换并追加到当前消息模型。 */
    void appendChatMsg(const std::shared_ptr<ChatDataBase> &message);
    /** @brief 将对应会话的历史页合并到模型，保留当前阅读锚点。 */
    void applyHistoryPage(int chatId,
                          const std::vector<std::shared_ptr<ChatDataBase>> &messages,
                          bool canLoadMore, qint64 nextCursor);
    /** @brief 结束历史页加载标志并恢复后续请求能力。 */
    void historyLoadFailed(int chatId);
    /** @brief 按当前发送者和 UUID 合并消息提交确认。 */
    void applyDeliveryAcknowledgements(int chatId,
                                       const QVector<MessageAcknowledgement> &acknowledgements);
    /** @brief 标记当前账号对应的失败消息。 */
    void markMessagesFailed(int chatId, const QVector<QString> &clientMessageIds);
    /** @brief 将持久化消息页合并到对应会话模型。 */
    void applyStoredHistory(int chatId, qint64 before, const QVector<StoredMessage> &messages, bool hasMore);
    /** @brief 返回当前模型中最早有效服务器消息 ID，无记录时返回 0。 */
    qint64 oldestLoadedMessageId(int chatId) const;

protected:
    /** @brief 绘制当前控件的背景、边框或内容，遵循 Qt GUI 线程事件约束。 */
    void paintEvent(QPaintEvent *event) override;

private slots:
    /** @brief 构造文本消息及 UUID，交给消息服务持久化发送。 */
    void on_send_btn_clicked();
    /** @brief 从当前会话分页状态请求较早本地历史。 */
    void requestOlderHistory();

signals:
    // 兼容现有 ChatInfo 缓存；消息显示与状态不再依赖该缓存。
    /** @brief 兼容现有 ChatInfo 缓存；消息显示与状态不再依赖该缓存。 */
    void outgoingTextQueued(QString, std::shared_ptr<ChatDataBase>);
    /** @brief 通知外部加载指定会话及游标的历史消息。 */
    void historyRequested(int chatId, qint64 beforeMessageId);

private:
    /** @brief 保存稳定消息身份及视口偏移，用于历史插入后恢复阅读位置。 */
    struct ScrollAnchor {
        qint64 messageId = 0;
        QString clientMessageId;
        int viewportOffset = 0;
        bool wasAtBottom = true;
        bool valid = false;
    };

    /** @brief 把旧消息对象转换为模型展示值，保留稳定身份和状态。 */
    MessageRecord toMessageRecord(const std::shared_ptr<ChatDataBase> &message);
    /** @brief 获取发送者头像缓存，用于构造消息展示值。 */
    QPixmap cachedAvatar(const QString &avatarKey);
    /** @brief 从当前会话的旧消息缓存初始化模型，不重复插入已存在身份。 */
    void seedModelFromLegacyData(MessageListModel *model,
                                 const std::shared_ptr<ChatInfo> &chatInfo);
    /** @brief 按当前会话及分页标志发起历史请求，避免同一页重复加载。 */
    void requestHistory(MessageListModel *model);
    /** @brief 记录当前可见消息的稳定身份和垂直偏移。 */
    ScrollAnchor captureScrollAnchor() const;
    /** @brief 保存当前会话阅读锚点，供页面切换后恢复。 */
    void saveCurrentScrollAnchor();
    /** @brief 按稳定身份恢复视口偏移，锚点失效时使用回退位置。 */
    void restoreScrollAnchor(int chatId, const ScrollAnchor &anchor);
    /** @brief 将滚动到底部排入事件队列，在布局更新后执行。 */
    void queueScrollToBottom(int chatId);

    /** @brief 创建账号资源传输管理器并连接上传下载事件。 */
    void initResourceTransfers();
    /** @brief 选择附件并为当前私聊启动资源上传。 */
    void selectResource();
    /** @brief 根据消息资源描述请求下载，并将校验后的本地文件用于展示。 */
    void loadResource(MessageRecord& record);
    ResourceTransferManager* _transfer = nullptr;
    QHash<QString, QJsonObject> _resourceDescriptors;
    QSet<int> _resourceChats;
    int _uploadChat = 0;
    int _uploadRecipient = 0;
    QString _uploadUuid;
    Ui::ChatPage *ui;
    std::shared_ptr<ChatInfo> _chatInfo;
    MessageModelStore _messageStore;
    MessageItemDelegate *_messageDelegate = nullptr;
    class MessageReadTracker *_readTracker = nullptr;
    QHash<int, ScrollAnchor> _scrollAnchors;
    QHash<QString, QPixmap> _avatarCache;
    QSet<int> _legacySeededChats;
    int _currentChatId = 0;
    bool _suppressHistoryRequests = false;
};

#endif // CHATPAGE_H
