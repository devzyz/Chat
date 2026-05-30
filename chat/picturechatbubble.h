#ifndef PICTURECHATBUBBLE_H
#define PICTURECHATBUBBLE_H

#include "global.h"
#include "chatbubble.h"
#include <QLabel>
#include "bubbleframe.h"

class PictureChatBubble : public ChatBubble
{
public:
    PictureChatBubble(ChatRole role, QPixmap pixmap, const QString& userName, const QString& userIcon, QWidget *parent = nullptr);

private:
    void setPixmapLabel(QPixmap pixmap);

    QLabel* _label;
    BubbleFrame* _wrapper;
};

#endif // PICTURECHATBUBBLE_H
