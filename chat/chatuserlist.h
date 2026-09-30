#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H

#include <QListWidget>

/** @brief 展示会话列表并在滚动到底部时请求下一页。 */
class ChatUserList : public QListWidget
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于展示会话列表并通知滚动分页。 */
    ChatUserList(QWidget *parent = nullptr);

signals:
    // 发送加载用户更多聊天信息的信号
    /** @brief 发送加载用户更多聊天信息的信号。 */
    void moreChatsRequested();

};

#endif // CHATUSERLIST_H
