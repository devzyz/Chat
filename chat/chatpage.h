#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include "userdata.h"

namespace Ui {
class ChatPage;
}

/**
 * @brief The ChatPage class
 * 右侧聊天区主界面
 */
class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();
    void SetChatInfo(std::shared_ptr<FriendInfo>);
    // 往QListWidget添加聊天记录
    void AppendChatMsg(std::shared_ptr<TextChatData>);

protected:
    void paintEvent(QPaintEvent * event) override;

private slots:
    void on_send_btn_clicked();

signals:
    void sig_append_send_text_chat_msg(std::shared_ptr<TextChatData>);

private:
    Ui::ChatPage *ui;
    std::shared_ptr<FriendInfo> _chat_info;
};

#endif // CHATPAGE_H
