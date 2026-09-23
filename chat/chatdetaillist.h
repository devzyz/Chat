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

signals:
    /** @brief 通知视口接近列表顶部，可以请求较早历史。 */
    void nearTopReached();
    /** @brief 通知消息视口尺寸改变，需要重新计算布局或滚动位置。 */
    void viewportResized();

protected:
    /** @brief 观察关联对象事件并处理本控件负责的交互，其余事件交回 Qt。 */
    bool eventFilter(QObject *watched, QEvent *event) override;
    /** @brief 响应控件尺寸变化并更新布局或通知视口变化。 */
    void resizeEvent(QResizeEvent *event) override;
};

#endif // CHATDETAILLIST_H
