#include "contactuserlist.h"
#include "listviewbehavior.h"
#include "logmgr.h"
#include "contactuseritem.h"
#include "grouptipitem.h"
#include <QListWidgetItem>
#include <QEvent>
#include "tcpmgr.h"
#include "usermgr.h"
#include "messageservice.h"
#include <QTimer>
#include <QCoreApplication>

ContactUserList::ContactUserList(QWidget *parent) : QListWidget(parent), _loading_contact(false)
{
    auto *behavior = new ListViewBehavior(this);
    connect(behavior, &ListViewBehavior::bottomReached, this, &ContactUserList::loadNextPage);

    loadContactUserList();

    // 连接点击item的信号和槽
    connect(this, &QListWidget::itemClicked, this, &ContactUserList::itemClicked);

    auto *messages = UserMgr::instance()->messages();
    connect(messages, &MessageService::directoryPageLoaded, this,
        /** @brief 按本地页推进联系人游标，重复记录只更新已有行。 */
        [this](const QString &kind, int after, const QJsonArray &rows, bool more) {
            if (kind != "contacts" || after != _cursor) return;
            for (const auto &value : rows) applyContact(value.toObject());
            if (!rows.isEmpty()) _cursor = rows.last().toObject()["id"].toInt();
            _hasMore = more;
            _loading_contact = false;
        });
    connect(messages, &MessageService::directoryChanged, this,
        /** @brief 已落盘的资料更新已显示行，新记录由本地分页读取。 */
        [this](const QJsonObject &directory) {
            const bool wasComplete = !_hasMore;
            for (const auto &value : directory["contacts"].toArray()) {
                const auto row = value.toObject();
                if (row["id"].toInt() <= _cursor) applyContact(row);
                else _hasMore = true;
            }
            if (_items.isEmpty() || wasComplete) loadNextPage();
        });
    connect(messages, &MessageService::directoryFailed, this,
        /** @brief 存储失败时结束当前列表等待，允许用户重试。 */
        [this](const QString &kind) { if (kind.isEmpty() || kind == "contacts") _loading_contact = false; });
    loadNextPage();
}

/**
 * @brief ContactUserList::showRedPoint
 * @param bshow
 * 这里的_add_friend_item保存的是新的朋友对应的item, 展示的是新的朋友右上角的红点
 */
void ContactUserList::showRedPoint(bool bshow)
{
    // 如果当前新的朋友已经被选中了，则直接返回
    if (_add_friend_item->isSelected()) {
        return ;
    }
    _add_friend_item_inner_widget->showRedPoint(bshow);
}

/**
 * @brief ContactUserList::addContactUserList
 * 模拟用户列表数据，有两种group分组
 * 新的朋友
 * 用户的联系人
 */
void ContactUserList::loadContactUserList()
{
    // 添加新的朋友分组标题item
    // 创建一个QListWidgetItem放入QListWidget,将item绑定到GroupTipItem上
    auto * newFriendGroupTip = new GroupTipItem();
    QListWidgetItem * new_friend_group_item = new QListWidgetItem();
    newFriendGroupTip->setGroupTip("新的朋友");
    new_friend_group_item->setSizeHint(newFriendGroupTip->sizeHint());
    this->addItem(new_friend_group_item);
    this->setItemWidget(new_friend_group_item, newFriendGroupTip);
    new_friend_group_item->setFlags(new_friend_group_item->flags() & -Qt::ItemIsSelectable);

    // 创建新的朋友分组下的item
    _add_friend_item_inner_widget = new ContactUserItem();
    _add_friend_item_inner_widget->setObjectName("new_friend_item");
    _add_friend_item_inner_widget->setInfo(0, tr("新的朋友"), ":/res/add_friend.png");
    _add_friend_item_inner_widget->setItemType(ListItemType::APPLY_FRIEND_ITEM);

    _add_friend_item = new QListWidgetItem();
    _add_friend_item->setSizeHint(_add_friend_item_inner_widget->sizeHint());
    this->addItem(_add_friend_item);
    this->setItemWidget(_add_friend_item, _add_friend_item_inner_widget);
    // 默认设置新的朋友申请条目被选中
    this->setCurrentItem(_add_friend_item);

    // 已添加联系人的groupItem
    auto * contactGroupTip = new GroupTipItem();
    contactGroupTip->setGroupTip("联系人");
    _contact_item = new QListWidgetItem();
    _contact_item->setSizeHint(contactGroupTip->sizeHint());
    this->addItem(_contact_item);
    this->setItemWidget(_contact_item, contactGroupTip);
    _contact_item->setFlags(_contact_item->flags() & ~Qt::ItemIsSelectable); // 设置为不可点击


}

/**
 * @brief ContactUserList::itemClicked
 * 点击QListWidget列表内item触发的槽函数
 */
void ContactUserList::itemClicked(QListWidgetItem * item)
{
    // 先转换为基类
    QWidget * widget = this->itemWidget(item);
    if (!widget) {
        SPDLOG_WARN("clicked contact list widget is null");
        return ;
    }

    // 对自定义widget进行操作，将item转化为基类的ListItemBase
    ListItemBase * customItem = qobject_cast<ListItemBase*> (widget);
    if (!customItem) {
        SPDLOG_WARN("clicked contact list item is null");
        return ;
    }

    // 判断是不是无效的类别或者是分组
    auto itemType = customItem->getItemType();
    if (itemType == ListItemType::INVALID_ITEM ||
        itemType == ListItemType::GROUP_TIP_ITEM) {
        SPDLOG_WARN("invalid contact list item clicked");
        return ;
    }

    // 查看新朋友的item被点击，发出信号
    if (itemType == ListItemType::APPLY_FRIEND_ITEM) {
        SPDLOG_DEBUG("friend application item clicked");
        ContactUserItem * contact_friend_item = qobject_cast<ContactUserItem*> (customItem);
        contact_friend_item->showRedPoint(false); // 点击后关闭红点提示
        emit friendApplicationsRequested();
        return ;
    }

    // 已有联系人的item被点击
    if (itemType == ListItemType::CONTACT_USER_ITEM) {
        ContactUserItem * contact_friend_item = qobject_cast<ContactUserItem*> (customItem);
        SPDLOG_DEBUG("contact user item clicked");
        emit friendDetailsRequested(contact_friend_item->getFriendInfo());
        return ;
    }
}

void ContactUserList::loadNextPage()
{
    if (_loading_contact || !_hasMore) return;
    _loading_contact = true;
    UserMgr::instance()->messages()->loadDirectoryPage("contacts", _cursor, LOADING_STEP_LENGTH);
}

void ContactUserList::applyContact(const QJsonObject &row)
{
    const int uid = row["id"].toInt();
    auto info = UserMgr::instance()->friendById(uid);
    auto *item = _items.value(uid, nullptr);
    if (row["is_self"].toBool() || (row.contains("relationship_active") && !row["relationship_active"].toBool())) {
        if (item) { _items.remove(uid); delete takeItem(this->row(item)); }
        return;
    }
    if (!info) return;
    if (!item) {
        auto *widget = new ContactUserItem();
        item = new QListWidgetItem();
        item->setSizeHint(widget->sizeHint());
        addItem(item);
        setItemWidget(item, widget);
        _items.insert(uid, item);
    }
    qobject_cast<ContactUserItem*>(itemWidget(item))->setInfo(info);
}
