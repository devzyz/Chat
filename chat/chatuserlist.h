#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H

#include <QListWidget>

/**
 * @brief The ChatUserList class
 * 显示用户聊天信息的QListWidget的自定义组件
 *
 *
 */
class ChatUserList : public QListWidget
{
    Q_OBJECT
public:
    ChatUserList(QWidget *parent = nullptr);

protected:
    /**
     * @brief eventFilter
     * @param watched
     * @param event
     * @return
     * 重写了滚轮的一些细节
     */
    bool eventFilter(QObject * watched, QEvent * event) override;

signals:
    // 发送加载用户更多聊天信息的信号
    void sig_loading_chat_user();
};

#endif // CHATUSERLIST_H
