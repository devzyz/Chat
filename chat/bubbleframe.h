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
    void setTextWidget(QWidget * text_edit);
    void setPictureWidget(QWidget * _picture_label);
protected:
    // 画出气泡聊天框
    void paintEvent(QPaintEvent * e);
private:
    // 创建水平布局
    QHBoxLayout *_h_layout;
    // 保存当前文本的模式（我 or 对方）
    ChatRole _role;
    // 内边距
    int _margin;
};

#endif // BUBBLEFRAME_H
