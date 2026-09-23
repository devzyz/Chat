#ifndef APPLYFRIENDLIST_H
#define APPLYFRIENDLIST_H

#include <QListWidget>

/**
 * @brief The ApplyFriendList class
 * 重写新的朋友的QListWidget，自定义的QListWidget
 */
class ApplyFriendList : public QListWidget
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于展示好友申请列表并观察列表交互。 */
    ApplyFriendList(QWidget * parent = nullptr);

protected:
    /** @brief 观察关联对象事件并处理本控件负责的交互，其余事件交回 Qt。 */
    bool eventFilter(QObject * watched, QEvent * event) override;

signals:
    /** @brief 通知上层切换到用户搜索入口。 */
    void searchRequested(bool);
};

#endif // APPLYFRIENDLIST_H
