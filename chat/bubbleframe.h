#ifndef BUBBLEFRAME_H
#define BUBBLEFRAME_H

#include <QFrame>
#include "global.h"
#include <QHBoxLayout>

/** @brief 为文本或图片控件绘制聊天气泡外框，由 Qt 父子关系拥有内容控件。 */
class BubbleFrame : public QFrame
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于为文本或图片控件绘制聊天气泡外框，由 Qt 父子关系拥有内容控件。 */
    BubbleFrame(ChatRole role, QWidget * parent = nullptr);
    /** @brief 将文本控件加入气泡布局，后续生命周期由 Qt 父子关系管理。 */
    void setTextWidget(QWidget * text_edit);
    /** @brief 将图片控件加入气泡布局，按图片内容更新展示。 */
    void setPictureWidget(QWidget * _picture_label);
protected:
    // 画出气泡聊天框
    /** @brief 绘制当前控件的背景、边框或内容，遵循 Qt GUI 线程事件约束。 */
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
