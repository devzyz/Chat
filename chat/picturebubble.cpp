#include "picturebubble.h"
#include <QLabel>

#define PIC_MAX_WIDTH 160
#define PIC_MAX_HEIGHT 90

PictureBubble::PictureBubble(const QPixmap &picture, ChatRole role, QWidget *parent) :
    BubbleFrame(role, parent)
{
    // 用label来显示图片
    QLabel * label = new QLabel();
    label->setScaledContents(true); // 自动缩放
    // 将图片缩放一下
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT), Qt::KeepAspectRatio);
    // 设置图片
    label->setPixmap(pix);
    this->setWidget(label);

    int left_margin = this->layout()->contentsMargins().left();
    int right_margin = this->layout()->contentsMargins().right();
    int v_margin = this->layout()->contentsMargins().bottom();
    // 设置气泡框的大小
    setFixedSize(pix.width() + left_margin + right_margin, pix.height() + v_margin * 2);
}
