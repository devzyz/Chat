#ifndef CHATITEMBASE_H
#define CHATITEMBASE_H

#include <QWidget>
#include "global.h"
#include <QLabel>

class BubbleFrame;

class ChatItemBase : public QWidget
{
    Q_OBJECT
public:
    explicit ChatItemBase(ChatRole role, QWidget * parent = nullptr);

    // 设置item的信息，包括用户名，头像
    void setUserName(const QString &name);
    void setUserIcon(const QPixmap &icon);

    // 用于设置文本widget的，其包含气泡框，文本等信息，通过一个widget一起管理
    void setWidget(QWidget *w);

private:
    ChatRole m_role;
    QLabel *m_pNameLabel;
    QLabel *m_pIconLabel;
    QWidget *m_pBubble;
};

#endif // CHATITEMBASE_H
