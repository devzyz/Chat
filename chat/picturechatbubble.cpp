#include "picturechatbubble.h"
#include <QLabel>

#define PIC_MAX_WIDTH 160
#define PIC_MAX_HEIGHT 90

PictureChatBubble::PictureChatBubble(ChatRole role, QPixmap picture, const QString& userName,
                                     const QString& userIcon, QWidget *parent) :  ChatBubble(role, parent){
    // 设置用户的头像和昵称
    setUserName(userName);
    setUserIcon(userIcon);

    _label = new QLabel();
    _label->setScaledContents(true); // 自动缩放

    // 创建交换区
    _wrapper = new BubbleFrame(role);
    _wrapper->setPictureWidget(_label);

    setPixmapLabel(picture); // 设置widget内部属性
    setWidget(_wrapper); // 将当前wrapper放到最外层的
}

void PictureChatBubble::setPixmapLabel(QPixmap picture)
{
    // 将图片缩放一下
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT), Qt::KeepAspectRatio);
    // 设置图片
    _label->setPixmap(pix);

    int pictureWidgetWidth = PIC_MAX_WIDTH;
    int pictureWidgetHeight = PIC_MAX_HEIGHT;

    int bubbleWidth = pictureWidgetWidth + 4 + 4 + 8;

    int bubbleHeight = qMax(10, pictureWidgetHeight + 4 + 4);

    // 设置尺寸大小
    _wrapper->setFixedSize(bubbleWidth, bubbleHeight);
}
