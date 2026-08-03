#ifndef CHATPAGE_H
#define CHATPAGE_H

#include "messagemodelstore.h"
#include "messagerecord.h"
#include "userdata.h"

#include <QHash>
#include <QSet>
#include <QWidget>
#include <memory>

namespace Ui {
class ChatPage;
}

class MessageItemDelegate;

/** 右侧聊天区主界面。 */
class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();

    void SetChatInfo(std::shared_ptr<ChatInfo> chatInfo);
    void AppendChatMsg(const std::shared_ptr<ChatDataBase> &message);
    void ApplyHistoryPage(int chatId,
                          const std::vector<std::shared_ptr<ChatDataBase>> &messages,
                          bool canLoadMore, qint64 nextCursor);
    void HistoryLoadFailed(int chatId);
    void ApplyDeliveryAcknowledgements(int chatId,
                                       const QVector<MessageAcknowledgement> &acknowledgements);
    void MarkMessagesFailed(int chatId, const QVector<QString> &clientMessageIds);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
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

    Ui::ChatPage *ui;
    std::shared_ptr<ChatInfo> _chatInfo;
    MessageModelStore _messageStore;
    MessageItemDelegate *_messageDelegate = nullptr;
    QHash<int, ScrollAnchor> _scrollAnchors;
    QHash<QString, QPixmap> _avatarCache;
    QSet<int> _legacySeededChats;
    int _currentChatId = 0;
    bool _suppressHistoryRequests = false;
};

#endif // CHATPAGE_H
