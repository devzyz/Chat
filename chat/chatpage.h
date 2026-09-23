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
    ~ChatPage();

    /** @brief 切换当前会话并恢复其消息与滚动状态。 */
    void SetChatInfo(std::shared_ptr<ChatInfo> chatInfo);
    void AppendChatMsg(const std::shared_ptr<ChatDataBase> &message);
    void ApplyHistoryPage(int chatId,
                          const std::vector<std::shared_ptr<ChatDataBase>> &messages,
                          bool canLoadMore, qint64 nextCursor);
    void HistoryLoadFailed(int chatId);
    /** @brief 按当前发送者和 UUID 合并消息提交确认。 */
    void ApplyDeliveryAcknowledgements(int chatId,
                                       const QVector<MessageAcknowledgement> &acknowledgements);
    /** @brief 标记当前账号对应的失败消息。 */
    void MarkMessagesFailed(int chatId, const QVector<QString> &clientMessageIds);
    /** @brief 将持久化消息页合并到对应会话模型。 */
    void applyStoredHistory(int chatId, qint64 before, const QVector<StoredMessage> &messages, bool hasMore);
    qint64 oldestLoadedMessageId(int chatId) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    /** @brief 构造文本消息及 UUID，交给消息服务持久化发送。 */
    void on_send_btn_clicked();
    void requestOlderHistory();

signals:
    // 兼容现有 ChatInfo 缓存；消息显示与状态不再依赖该缓存。
    void sig_append_send_text_cache_msg(QString, std::shared_ptr<ChatDataBase>);
    void sig_request_history(int chatId, qint64 beforeMessageId);

private:
    struct ScrollAnchor {
        qint64 messageId = 0;
        QString clientMessageId;
        int viewportOffset = 0;
        bool wasAtBottom = true;
        bool valid = false;
    };

    MessageRecord toMessageRecord(const std::shared_ptr<ChatDataBase> &message);
    QPixmap cachedAvatar(const QString &avatarKey);
    void seedModelFromLegacyData(MessageListModel *model,
                                 const std::shared_ptr<ChatInfo> &chatInfo);
    void requestHistory(MessageListModel *model);
    ScrollAnchor captureScrollAnchor() const;
    void saveCurrentScrollAnchor();
    void restoreScrollAnchor(int chatId, const ScrollAnchor &anchor);
    void queueScrollToBottom(int chatId);

    /** @brief 创建账号资源传输管理器并连接上传下载事件。 */
    void initResourceTransfers();
    /** @brief 选择附件并为当前私聊启动资源上传。 */
    void selectResource();
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
