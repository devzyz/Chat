#ifndef CHATBUBBLE_H
#define CHATBUBBLE_H

#include <QWidget>
#include "global.h"

namespace Ui {
class Chat_Bubble_Widget;
}

class ChatBubble : public QWidget
{
    Q_OBJECT
public:
    explicit ChatBubble(ChatRole role, QWidget *parent = nullptr);
    ~ChatBubble();

    void setWidget(QWidget *w);
    void setUserName(const QString &name);
    void setUserIcon(const QPixmap &icon);

protected:
    ChatRole _role;
    QWidget* _context_widget;

private:
    Ui::Chat_Bubble_Widget *ui;
};

#endif // CHATBUBBLE_H
