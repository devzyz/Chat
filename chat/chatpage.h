#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>

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

protected:
    void paintEvent(QPaintEvent * event);

private slots:
    void on_send_btn_clicked();

private:
    Ui::ChatPage *ui;
};

#endif // CHATPAGE_H
