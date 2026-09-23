#ifndef PICTURECHATBUBBLE_H
#define PICTURECHATBUBBLE_H

#include "global.h"
#include "chatbubble.h"
#include <QLabel>
#include "bubbleframe.h"

/** @brief 将图片标签嵌入聊天气泡并提供图片展示。 */
class PictureChatBubble : public ChatBubble
{
public:
    /** @brief 初始化对象，用于将图片标签嵌入聊天气泡并提供图片展示。 */
    PictureChatBubble(ChatRole role, QPixmap pixmap, const QString& userName, const QString& userIcon, QWidget *parent = nullptr);

private:
    /** @brief 设置气泡中的图片标签内容与布局。 */
    void setPixmapLabel(QPixmap pixmap);

    QLabel* _label;
    BubbleFrame* _wrapper;
};

#endif // PICTURECHATBUBBLE_H
