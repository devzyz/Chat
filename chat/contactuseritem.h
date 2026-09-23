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
    /** @brief 初始化对象，用于展示好友或联系人条目的资料。 */
    explicit ContactUserItem(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ContactUserItem();
    /**
     * @brief sizeHint
     * @return
     * 用于返回给QListWidgetItem，确定其的大小
     */
    QSize sizeHint() const override;
    // 添加好友后，设置好友信息
    /** @brief 添加好友后，设置好友信息。 */
    void SetInfo(std::shared_ptr<AuthInfo> auth_info);
    /**
     * @brief SetInfo
     * 设置当前item的信息
     */
    void SetInfo(int uid, QString name, QString icon);
    // 设置当前item的信息
    /** @brief 设置当前item的信息。 */
    void SetInfo(std::shared_ptr<UserInfo>);
    /**
     * @brief ShowRedPoint
     * 显示当前item的红点
     */
    void ShowRedPoint(bool show = false);
    // 获取当前item的信息
    /** @brief 获取当前item的信息。 */
    std::shared_ptr<UserInfo> GetFriendInfo();

private:
    Ui::ContactUserItem *ui;
    // 当前item保存的用户信息
    std::shared_ptr<UserInfo> _friend_info;
};

#endif // CONTACTUSERITEM_H
