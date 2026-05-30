#include "contactuserlist.h"
#include "contactuseritem.h"
#include "grouptipitem.h"
#include <QListWidgetItem>
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include "tcpmgr.h"
#include "usermgr.h"
#include <QTimer>
#include <QCoreApplication>

ContactUserList::ContactUserList(QWidget *parent) : QListWidget(parent), _loading_contact(false)
{
    // 关闭滚动条
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 安装事件过滤器
    this->viewport()->installEventFilter(this);

    addContactUserList();

    // 连接点击item的信号和槽
    connect(this, &QListWidget::itemClicked, this, &ContactUserList::slot_item_clicked);

    // 连接认证的服务器回包处理发出的更新信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_add_auth_friend,
            this, &ContactUserList::slot_tcp_add_auth_friend);
    // 连接通知认证的服务器回包处理发出的更新信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_notify_auth_friend,
            this, &ContactUserList::slot_tcp_notify_auth_friend);
}

/**
 * @brief ContactUserList::ShowRedPoint
 * @param bshow
 * 这里的_add_friend_item保存的是新的朋友对应的item, 展示的是新的朋友右上角的红点
 */
void ContactUserList::ShowRedPoint(bool bshow)
{
    // 如果当前新的朋友已经被选中了，则直接返回
    if (_add_friend_item->isSelected()) {
        return ;
    }
    _add_friend_item_inner_widget->ShowRedPoint(bshow);
}

/**
 * @brief ContactUserList::eventFilter
 * @param watched
 * @param event
 * @return
 * 重写QListWidget的鼠标移入移出（是否显示滚轮）
 * 重写在QListWidget内鼠标的滚动事件
 */
bool ContactUserList::eventFilter(QObject *watched, QEvent *event)
{
    // 如果鼠标进入了这个列表的显示窗口区域
    if (watched == this->viewport()) {
        if (event->type() == QEvent::Enter) {
            // 鼠标悬浮在当前窗口，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }else if(event->type() == QEvent::Leave){
            // 鼠标不在当前窗口，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 检查鼠标在显示窗口内的滚轮事件
    if (watched == this->viewport() && event->type() == QEvent::Wheel) {
        // 将基类QEvent转换为对应的子类，QWheelEvent来访问他的私有数据
        QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
        // 计算移动的步长
        // angleDeltada返回的单位为1/8度，因此，除以8能够得到角度，然后一般鼠标滚轮嘎达一下为15度，因此除以15得到步数
        int numDegrees = wheelEvent->angleDelta().y() / 8;
        int numSteps = numDegrees / 15;

        // 设置滚动幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        // 检查是否滚动到底部
        QScrollBar *scrollBar = this->verticalScrollBar();
        int maxScrollValue = scrollBar->maximum();
        int currentValue = scrollBar->value();

        if (maxScrollValue - currentValue <= 0) {
            // 滚动到底部，加载新的联系人
            qDebug() << "load more contact user";
            // 判断联系人是否加载完成
            auto isLoadingFinish = UserMgr::GetInstance()->ContactIsLoadFinish();
            if (isLoadingFinish) {
                return true;
            }
            // 判断当前是否已正在加载
            if (_loading_contact) {
                return true;
            }
            _loading_contact = true;
            QTimer::singleShot(100, this, [this]() {
                _loading_contact = false;
            });
            // 发送信号通知聊天界面加载更多聊天内容
            emit sig_loading_contact_list();
        }

        return true;
    }

    return QListWidget::eventFilter(watched, event);
}

/**
 * @brief ContactUserList::addContactUserList
 * 模拟用户列表数据，有两种group分组
 * 新的朋友
 * 用户的联系人
 */
void ContactUserList::addContactUserList()
{
    // 添加新的朋友分组标题item
    // 创建一个QListWidgetItem放入QListWidget,将item绑定到GroupTipItem上
    auto * newFriendGroupTip = new GroupTipItem();
    QListWidgetItem * new_friend_group_item = new QListWidgetItem();
    newFriendGroupTip->SetGroupTip("新的朋友");
    new_friend_group_item->setSizeHint(newFriendGroupTip->sizeHint());
    this->addItem(new_friend_group_item);
    this->setItemWidget(new_friend_group_item, newFriendGroupTip);
    new_friend_group_item->setFlags(new_friend_group_item->flags() & -Qt::ItemIsSelectable);

    // 创建新的朋友分组下的item
    _add_friend_item_inner_widget = new ContactUserItem();
    _add_friend_item_inner_widget->setObjectName("new_friend_item");
    _add_friend_item_inner_widget->SetInfo(0, tr("新的朋友"), ":/res/add_friend.png");
    _add_friend_item_inner_widget->SetItemType(ListItemType::APPLY_FRIEND_ITEM);

    _add_friend_item = new QListWidgetItem();
    _add_friend_item->setSizeHint(_add_friend_item_inner_widget->sizeHint());
    this->addItem(_add_friend_item);
    this->setItemWidget(_add_friend_item, _add_friend_item_inner_widget);
    // 默认设置新的朋友申请条目被选中
    this->setCurrentItem(_add_friend_item);

    // 已添加联系人的groupItem
    auto * contactGroupTip = new GroupTipItem();
    contactGroupTip->SetGroupTip("联系人");
    _contact_item = new QListWidgetItem();
    _contact_item->setSizeHint(contactGroupTip->sizeHint());
    this->addItem(_contact_item);
    this->setItemWidget(_contact_item, contactGroupTip);
    _contact_item->setFlags(_contact_item->flags() & ~Qt::ItemIsSelectable); // 设置为不可点击

    auto contact_list = UserMgr::GetInstance()->GetSomeContactList();
    if (!contact_list.empty()) {
        for (auto &info : contact_list) {
            auto _contact_user_item = new ContactUserItem();
            _contact_user_item->SetInfo(info);
            _contact_user_item->SetItemType(ListItemType::CONTACT_USER_ITEM);

            QListWidgetItem * _friend_item = new QListWidgetItem();
            _friend_item->setSizeHint(_contact_user_item->sizeHint());
            this->addItem(_friend_item);
            this->setItemWidget(_friend_item, _contact_user_item);
        }
    }
}

/**
 * @brief ContactUserList::slot_item_clicked
 * 点击QListWidget列表内item触发的槽函数
 */
void ContactUserList::slot_item_clicked(QListWidgetItem * item)
{
    // 先转换为基类
    QWidget * widget = this->itemWidget(item);
    if (!widget) {
        qDebug() << "slot item clicked widget is nullptr";
        return ;
    }

    // 对自定义widget进行操作，将item转化为基类的ListItemBase
    ListItemBase * customItem = qobject_cast<ListItemBase*> (widget);
    if (!customItem) {
        qDebug() << "slot item clicked widget is nullptr";
        return ;
    }

    // 判断是不是无效的类别或者是分组
    auto itemType = customItem->GetItemType();
    if (itemType == ListItemType::INVALID_ITEM ||
        itemType == ListItemType::GROUP_TIP_ITEM) {
        qDebug() << "slot invalid item clicked";
        return ;
    }

    // 查看新朋友的item被点击，发出信号
    if (itemType == ListItemType::APPLY_FRIEND_ITEM) {
        qDebug() << "apply friend item clicked";
        ContactUserItem * contact_friend_item = qobject_cast<ContactUserItem*> (customItem);
        contact_friend_item->ShowRedPoint(false); // 点击后关闭红点提示
        emit sig_switch_apply_friend_list_page();
        return ;
    }

    // 已有联系人的item被点击
    if (itemType == ListItemType::CONTACT_USER_ITEM) {
        ContactUserItem * contact_friend_item = qobject_cast<ContactUserItem*> (customItem);
        qDebug() << "contact user item clicked";
        emit sig_switch_friend_info_page(contact_friend_item->GetFriendInfo());
        return ;
    }
}

// 自己验证的添加
void ContactUserList::slot_tcp_add_auth_friend(std::shared_ptr<AuthInfo> auth_info)
{
    qDebug() << "slot add auth friend";
    bool isFriend = UserMgr::GetInstance()->CheckIsFriendById(auth_info->_uid);
    // 如果已经是好友了，则跳过
    if (isFriend) {
        return ;
    }

    // 否则更新contactlist列表
    auto * contact_user_item = new ContactUserItem();
    contact_user_item->SetInfo(auth_info);
    contact_user_item->SetItemType(ListItemType::CONTACT_USER_ITEM);

    QListWidgetItem *item = new QListWidgetItem();
    item->setSizeHint(contact_user_item->sizeHint());

    int index = this->row(_contact_item);
    // 在_contact_item后插入"新的朋友"
    this->insertItem(index + 1, item);
    // 设置QListWidgetItem的widget为自定义的widget
    this->setItemWidget(item, contact_user_item);
}

// 服务器通知的添加
void ContactUserList::slot_tcp_notify_auth_friend(std::shared_ptr<AuthInfo> auth_info)
{
    qDebug() << "slot notify auth friend";
    bool isFriend = UserMgr::GetInstance()->CheckIsFriendById(auth_info->_uid);
    // 如果已经是好友了，则跳过
    if (isFriend) {
        return ;
    }

    // 否则更新contactlist列表
    auto * contact_user_item = new ContactUserItem();
    contact_user_item->SetInfo(auth_info);
    contact_user_item->SetItemType(ListItemType::CONTACT_USER_ITEM);

    QListWidgetItem *item = new QListWidgetItem();
    item->setSizeHint(contact_user_item->sizeHint());

    int index = this->row(_contact_item);
    // 在_contact_item后插入新的朋友
    this->insertItem(index + 1, item);
    // 设置QListWidgetItem的widget为自定义的widget
    this->setItemWidget(item, contact_user_item);
}
