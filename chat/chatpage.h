#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include "userdata.h"
#include <QListWidgetItem>

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
    void SetChatInfo(std::shared_ptr<ChatInfo>);
    // 往QListWidget添加聊天记录
    void AppendChatMsg(std::shared_ptr<ChatDataBase>);
    // 更新未读状态
    void UpdateChatUnreadStatus(std::vector<QString>&);
protected:
    void paintEvent(QPaintEvent * event) override;

private slots:
    void on_send_btn_clicked();

signals:
    void sig_append_send_text_cache_msg(QString, std::shared_ptr<ChatDataBase>);

private:
    Ui::ChatPage *ui;
    std::shared_ptr<ChatInfo> _chat_info;
    QMap<QString, QWidget*> _cache_chat_msg;
};

#endif // CHATPAGE_H
