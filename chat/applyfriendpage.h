#ifndef APPLYFRIENDPAGE_H
#define APPLYFRIENDPAGE_H

#include <QWidget>
#include "applyfrienditem.h"

namespace Ui {
class ApplyFriendPage;
}

/** @brief 展示当前账号收到的好友申请并同步审批结果。 */
class ApplyFriendPage : public QWidget
{
    Q_OBJECT
public:
    /** @brief 创建好友申请页并连接审批结果通知。 */
    explicit ApplyFriendPage(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ApplyFriendPage();
    /** @brief 将新的好友申请加入页面，已有申请不重复创建。 */
    void addNewApply(std::shared_ptr<ApplyInfo>);

protected:
    /** @brief 绘制当前控件的背景、边框或内容，遵循 Qt GUI 线程事件约束。 */
    void paintEvent(QPaintEvent * event) override;

private:
    /** @brief 从账号缓存加载好友申请列表。 */
    void loadApplyList();
    QMap<int, ApplyFriendItem*> _apply_items_map;

private:
    Ui::ApplyFriendPage *ui;

signals:
    /** @brief 通知上层切换到用户搜索入口。 */
    void searchRequested(bool);

public slots:
    /** @brief 接收好友审批成功结果并刷新申请状态。 */
    void authFinish(std::shared_ptr<AuthInfo>);
};

#endif // APPLYFRIENDPAGE_H
