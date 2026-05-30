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
    explicit ApplyFriendItem(QWidget *parent = nullptr);
    ~ApplyFriendItem();
    /**
     * @brief SetInfo
     * @param apply_info
     * 设置当前item的信息
     */
    void SetInfo(std::shared_ptr<ApplyInfo> apply_info);
    /**
     * @brief ShowAddBtn
     * @param bshow
     * 是否显示添加按钮，当点击添加后，将添加按钮隐藏，显示已添加的label
     */
    void ShowAddBtn(bool bshow);
    /**
     * @brief sizeHint
     * @return
     * 为了设置外部的QListWidgetItem
     */
    QSize sizeHint() const override;
    /**
     * @brief GetUid
     * @return
     * 获取uid
     */
    int GetUid();

private:
    Ui::ApplyFriendItem *ui;
    // 当前item显示的，对应的好友申请信息
    std::shared_ptr<ApplyInfo> _apply_info;
    bool _added; // 是否已添加

signals:
    // 点击添加按钮后，发出认证成功信号
    void sig_auth_friend(std::shared_ptr<ApplyInfo> apply_info);
};

#endif // APPLYFRIENDITEM_H
