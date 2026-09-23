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

protected:
    /** @brief 处理滚动事件，在尚有会话可加载时通知分页请求。 */
    bool eventFilter(QObject * watched, QEvent * event) override;

signals:
    // 发送加载用户更多聊天信息的信号
    /** @brief 发送加载用户更多聊天信息的信号。 */
    void sig_loading_chat_list();

private:
    bool _loading_chat;
};

#endif // CHATUSERLIST_H
