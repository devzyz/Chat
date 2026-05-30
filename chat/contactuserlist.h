#ifndef CONTACTUSERLIST_H
#define CONTACTUSERLIST_H

#include <QListWidget>
#include "userdata.h"

class ContactUserItem;

/**
 * @brief The ContactUserList class
 * 这个是联系人列表，自定义的QListWidget组件
 */
class ContactUserList : public QListWidget
{
    Q_OBJECT
public:
    ContactUserList(QWidget * parent = nullptr);
    /**
     * @brief ShowRedPoint
     * @param bshow
     * _add_friend_item保存的是新的朋友那个item
     * 当有新的请求过来时，调用当前函数，设置红点
     */
    void ShowRedPoint(bool bshow = true);

protected:
    /**
     * @brief eventFilter
     * @param watched
     * @param event
     * @return
     *
     */
    bool eventFilter(QObject * watched, QEvent * event);

private:
    /**
     * @brief addContactUserList
     * 模拟添加新的已有联系人列表
     */
    void addContactUserList();
    bool _loading_contact;

public slots:
    /**
     * @brief slot_item_clicked
     * @param item
     * 当某个item被点击后，触发的槽函数
     */
    void slot_item_clicked(QListWidgetItem * item);
    /**
     * @brief slot_tcp_add_auth_friend
     * 当我点击好友同意后，服务器回包后，添加对方为好友
     */
    void slot_tcp_add_auth_friend(std::shared_ptr<AuthInfo> );
    /**
     * @brief slot_tcp_notify_auth_friend
     * 当对方点击同意后，服务器通知我，添加对方为好友
     */
    void slot_tcp_notify_auth_friend(std::shared_ptr<AuthInfo>);
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
    void sig_switch_friend_info_page(std::shared_ptr<FriendInfo>);

private:
    // 保存的是新的朋友item
    QListWidgetItem * _add_friend_item;
    ContactUserItem * _add_friend_item_inner_widget;
    // 用于保存联系人组的item，以后插入在该组的下方插入
    QListWidgetItem * _contact_item;
};

#endif // CONTACTUSERLIST_H
