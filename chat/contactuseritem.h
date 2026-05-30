#ifndef CONTACTUSERITEM_H
#define CONTACTUSERITEM_H

#include <QWidget>
#include "listitembase.h"
#include "userdata.h"

namespace Ui {
class ContactUserItem;
}

/**
 * @brief The ContactUserItem class
 * 联系连列表的自定义QListWidgetItem
 */
class ContactUserItem : public ListItemBase
{
    Q_OBJECT
public:
    explicit ContactUserItem(QWidget *parent = nullptr);
    ~ContactUserItem();
    /**
     * @brief sizeHint
     * @return
     * 用于返回给QListWidgetItem，确定其的大小
     */
    QSize sizeHint() const override;
    // 添加好友后，设置好友信息
    void SetInfo(std::shared_ptr<AuthInfo> auth_info);
    /**
     * @brief SetInfo
     * @param uid
     * @param name
     * @param icon
     * 设置当前item的信息
     */
    void SetInfo(int uid, QString name, QString icon);
    // 设置当前item的信息
    void SetInfo(std::shared_ptr<FriendInfo>);
    /**
     * @brief ShowRedPoint
     * @param show
     * 显示当前item的红点
     */
    void ShowRedPoint(bool show = false);
    // 获取当前item的信息
    std::shared_ptr<FriendInfo> GetFriendInfo();

private:
    Ui::ContactUserItem *ui;
    // 当前item保存的用户信息
    std::shared_ptr<FriendInfo> _friend_info;
};

#endif // CONTACTUSERITEM_H
