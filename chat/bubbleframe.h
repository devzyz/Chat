#ifndef BUBBLEFRAME_H
#define BUBBLEFRAME_H

#include <QFrame>
#include "global.h"
#include <QHBoxLayout>

class BubbleFrame : public QFrame
{
    Q_OBJECT
public:
    BubbleFrame(ChatRole role, QWidget * parent = nullptr);
    void setWidget(QWidget * w);
protected:
    // 画出气泡聊天框
    void paintEvent(QPaintEvent * e);
private:
    // 内部用于存放聊天文本
    QHBoxLayout *m_pHLayout;
    ChatRole m_role;
    int m_margin;
};

#endif // BUBBLEFRAME_H
