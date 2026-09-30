#pragma once

#include <QObject>

class QAbstractItemView;

/** @brief 为列表复用悬停滚动条和到底通知；由视图持有，不管理业务数据或加载状态。 */
class ListViewBehavior final : public QObject
{
    Q_OBJECT
public:
    /** @brief 绑定视图，保留 Qt 原生滚动、选择及键盘行为；视图必须非空。 */
    explicit ListViewBehavior(QAbstractItemView *view);

signals:
    /** @brief 可见列表滚动到底或在底部继续向下滚动时通知；调用方负责加载去重和结束判断。 */
    void bottomReached();

protected:
    /** @brief 观察悬停和向下滚轮，不消费事件或手动修改滚动位置。 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** @brief 等 Qt 完成滚动后检查底部，同一轮事件合并检查；销毁时自动取消。 */
    void queueBottomCheck();
    QAbstractItemView *_view;
    bool _bottomCheckQueued = false;
};
