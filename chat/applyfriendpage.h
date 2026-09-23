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
    ~ApplyFriendPage();
    /** @brief 将新的好友申请加入页面，已有申请不重复创建。 */
    void AddNewApply(std::shared_ptr<ApplyInfo>);

protected:
    void paintEvent(QPaintEvent * event) override;

private:
    /** @brief 从账号缓存加载好友申请列表。 */
    void loadApplyList();
    QMap<int, ApplyFriendItem*> _apply_items_map;

private:
    Ui::ApplyFriendPage *ui;

signals:
    void sig_show_search(bool);

public slots:
    void slot_auth_finish(std::shared_ptr<AuthInfo>);
};

#endif // APPLYFRIENDPAGE_H
