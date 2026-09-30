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
     * @brief showRedPoint
     * _add_friend_item保存的是新的朋友那个item
     * 当有新的请求过来时，调用当前函数，设置红点
     */
    void showRedPoint(bool bshow = true);
    /** @brief 从账号 SQLite 请求下一页联系人，查询完成前不重复请求。 */
    void loadNextPage();

private:
    /** @brief 从账号缓存加载一页联系人并推进游标。 */
    void loadContactUserList();
    bool _loading_contact;
    bool _hasMore = true;
    int _cursor = 0;
    QMap<int, QListWidgetItem*> _items;
    /** @brief 按 UID 插入或更新本地联系人，保留现有选中项。 */
    void applyContact(const QJsonObject &row);

public slots:
    /**
     * @brief itemClicked
     * 当某个item被点击后，触发的槽函数
     */
    void itemClicked(QListWidgetItem * item);
signals:
    /**
     * @brief sig_switch_apply_friend_page
     * 将右侧界面切换为新朋友申请列表
     */
    void friendApplicationsRequested();
    /**
     * @brief friendDetailsRequested
     * 将右侧界面切换为已有联系人具体信息列表
     */
    void friendDetailsRequested(std::shared_ptr<UserInfo>);

private:
    // 保存的是新的朋友item
    QListWidgetItem * _add_friend_item;
    ContactUserItem * _add_friend_item_inner_widget;
    // 用于保存联系人组的item，以后插入在该组的下方插入
    QListWidgetItem * _contact_item;
};

#endif // CONTACTUSERLIST_H
