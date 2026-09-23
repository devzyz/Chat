#ifndef APPLYFRIENDITEM_H
#define APPLYFRIENDITEM_H

#include <QWidget>
#include "userdata.h"
#include "listitembase.h"

namespace Ui {
class ApplyFriendItem;
}

/**
 * @brief The ApplyFriendItem class
 * 好友请求列表重写item
 */
class ApplyFriendItem : public ListItemBase
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于展示一条好友申请及审批入口。 */
    explicit ApplyFriendItem(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ApplyFriendItem();
    /**
     * @brief setInfo
     * 设置当前item的信息
     */
    void setInfo(std::shared_ptr<ApplyInfo> apply_info);
    /**
     * @brief showAddBtn
     * 是否显示添加按钮，当点击添加后，将添加按钮隐藏，显示已添加的label
     */
    void showAddBtn(bool bshow);
    /**
     * @brief sizeHint
     * @return
     * 为了设置外部的QListWidgetItem
     */
    QSize sizeHint() const override;
    /**
     * @brief getUid
     * @return
     * 获取uid
     */
    int getUid();

private:
    Ui::ApplyFriendItem *ui;
    // 当前item显示的，对应的好友申请信息
    std::shared_ptr<ApplyInfo> _apply_info;
    bool _added; // 是否已添加

signals:
    // 点击添加按钮后，发出认证成功信号
    /** @brief 点击添加按钮后，发出认证成功信号。 */
    void friendApprovalRequested(std::shared_ptr<ApplyInfo> apply_info);
};

#endif // APPLYFRIENDITEM_H
