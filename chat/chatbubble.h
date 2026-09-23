#ifndef CHATBUBBLE_H
#define CHATBUBBLE_H

#include <QWidget>
#include "global.h"

namespace Ui {
class Chat_Bubble_Widget;
}

/** @brief 组合发送者头像、名称、内容控件和消息状态的聊天展示组件。 */
class ChatBubble : public QWidget
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于组合发送者头像、名称、内容控件和消息状态的聊天展示组件。 */
    explicit ChatBubble(ChatRole role, QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ChatBubble();

    /** @brief 将消息内容控件放入聊天气泡布局。 */
    void setWidget(QWidget *w);
    /** @brief 更新发送者名称显示。 */
    void setUserName(const QString &name);
    /** @brief 更新发送者头像显示。 */
    void setUserIcon(const QPixmap &icon);
    /** @brief 更新消息发送状态及相应图标。 */
    void setChatStatus(ChatStatus);
protected:
    ChatRole _role;
    QWidget* _context_widget;

private:
    Ui::Chat_Bubble_Widget *ui;
};

#endif // CHATBUBBLE_H
