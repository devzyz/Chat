#ifndef CONTACTUSERLIST_H
#define CONTACTUSERLIST_H

#include <QListWidget>
#include "userdata.h"

class ContactUserItem;

/** @brief 展示新朋友入口与联系人列表，管理分页和选择通知。 */
class ContactUserList : public QListWidget
{
    Q_OBJECT
public:
    /** @brief 创建联系人列表并连接好友审批及分页事件。 */
    ContactUserList(QWidget * parent = nullptr);
    /**
     * @brief ShowRedPoint
     * _add_friend_item保存的是新的朋友那个item
     * 当有新的请求过来时，调用当前函数，设置红点
     */
    void ShowRedPoint(bool bshow = true);

protected:
    /** @brief 处理滚动事件，在联系人未全部加载时请求下一页。 */
    bool eventFilter(QObject * watched, QEvent * event);

private:
    /** @brief 从账号缓存加载一页联系人并推进游标。 */
    void LoadContactUserList();
    bool _loading_contact;
    /** @brief 为通过审批的好友创建联系人列表项。 */
    void AddNewContact(std::shared_ptr<AuthInfo>);

public slots:
    /**
     * @brief slot_item_clicked
     * 当某个item被点击后，触发的槽函数
     */
    void slot_item_clicked(QListWidgetItem * item);
    /**
     * @brief slot_tcp_add_auth_friend
     * 添加对方为好友通知
     */
    void slot_tcp_add_friend(std::shared_ptr<AuthInfo> );
signals:
    /**
     * @brief sig_loading_contact_user
     * 加载更多联系人的信号
     */
    void sig_loading_contact_list();
    /**
     * @brief sig_switch_apply_friend_page
     * 将右侧界面切换为新朋友申请列表
     */
    void sig_switch_apply_friend_list_page();
    /**
     * @brief sig_switch_friend_info_page
     * 将右侧界面切换为已有联系人具体信息列表
     */
    void sig_switch_friend_info_page(std::shared_ptr<UserInfo>);

private:
    // 保存的是新的朋友item
    QListWidgetItem * _add_friend_item;
    ContactUserItem * _add_friend_item_inner_widget;
    // 用于保存联系人组的item，以后插入在该组的下方插入
    QListWidgetItem * _contact_item;
};

#endif // CONTACTUSERLIST_H
