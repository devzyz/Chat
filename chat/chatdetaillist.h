#ifndef CHATDETAILLIST_H
#define CHATDETAILLIST_H

#include <QListView>

/** @brief 观察消息视图滚动和尺寸变化，向页面提供历史加载与滚动定位事件。 */
class ChatDetailList : public QListView
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于观察消息视图滚动和尺寸变化，向页面提供历史加载与滚动定位事件。 */
    explicit ChatDetailList(QWidget *parent = nullptr);

    /** @brief 判断滚动位置是否接近消息列表底部，供自动滚动策略使用。 */
    bool isNearBottom(int tolerance = 24) const;
    /** @brief 复制当前文字或附件文件名，不暴露内部资源描述。 */
    void copyCurrentMessage();

signals:
    /** @brief 通知视口接近列表顶部，可以请求较早历史。 */
    void nearTopReached();
    /** @brief 通知消息视口尺寸改变，需要重新计算布局或滚动位置。 */
    void viewportResized();

protected:
    /** @brief 响应控件尺寸变化并更新布局或通知视口变化。 */
    void resizeEvent(QResizeEvent *event) override;
    /** @brief 处理标准复制快捷键，其余按键交给原生列表。 */
    void keyPressEvent(QKeyEvent *event) override;
    /** @brief 为鼠标指向的消息提供复制入口。 */
    void contextMenuEvent(QContextMenuEvent *event) override;
};

#endif // CHATDETAILLIST_H
