#include "contactuserlist.h"
#include "contactuseritem.h"
#include "grouptipitem.h"
#include <QListWidgetItem>
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>

ContactUserList::ContactUserList(QWidget *parent) : QListWidget(parent)
{
    // 关闭滚动条
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 安装事件过滤器
    this->viewport()->installEventFilter(this);

    // 模拟数据
    addContactUserList();

    // 连接点击item的信号和槽
    connect(this, &QListWidget::itemClicked, this, &ContactUserList::slot_item_clicked);
}

/**
 * @brief ContactUserList::ShowRedPoint
 * @param bshow
 * 展示tiem右上角的红点
 */
void ContactUserList::ShowRedPoint(bool bshow)
{
    _add_friend_item->ShowRedPoint(bshow);
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
            // 发送信号通知聊天界面加载更多聊天内容
            emit sig_loading_contact_user();
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
    new_friend_group_item->setSizeHint(newFriendGroupTip->sizeHint());
    this->addItem(new_friend_group_item);
    this->setItemWidget(new_friend_group_item, newFriendGroupTip);

    new_friend_group_item->setFlags(new_friend_group_item->flags() & -Qt::ItemIsSelectable);

    // 创建新的朋友分组下的item
    _add_friend_item = new ContactUserItem();
    _add_friend_item->setObjectName("new_friend_item");
    _add_friend_item->SetInfo(0, tr("新的朋友"), ":/res/add_friend.png");
    _add_friend_item->SetItemType(ListItemType::APPLY_FRIEND_ITEM);

    QListWidgetItem * new_friend_item = new QListWidgetItem();
    new_friend_item->setSizeHint(_add_friend_item->sizeHint());
    this->addItem(new_friend_item);
    this->setItemWidget(new_friend_item, _add_friend_item);
    // 默认设置新的朋友申请条目被选中
    this->setCurrentItem(new_friend_item);

    auto * contactGroupTip = new GroupTipItem();
    QListWidgetItem * contact_group_item = new QListWidgetItem();
    contact_group_item->setSizeHint(contactGroupTip->sizeHint());
    this->addItem(contact_group_item);
    this->setItemWidget(contact_group_item, contactGroupTip);

    contact_group_item->setFlags(contact_group_item->flags() & -Qt::ItemIsSelectable);

    for (int i = 0; i < 13; i ++ ) {
        _add_friend_item = new ContactUserItem();
        _add_friend_item->setObjectName("new_friend_item");
        _add_friend_item->SetInfo(0, tr("测试人"), ":/res/head_1.jpg");
        _add_friend_item->SetItemType(ListItemType::CONTACT_USER_ITEM);

        QListWidgetItem * _friend_item = new QListWidgetItem();
        _friend_item->setSizeHint(_add_friend_item->sizeHint());
        this->addItem(_friend_item);
        this->setItemWidget(_friend_item, _add_friend_item);
    }
}

/**
 * @brief ContactUserList::slot_item_clicked
 * 点击QListWidget列表内item触发的槽函数
 */
void ContactUserList::slot_item_clicked(QListWidgetItem * item)
{
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
        emit sig_switch_apply_friend_page();
        return ;
    }

    // 已有联系人的item被点击
    if (itemType == ListItemType::CONTACT_USER_ITEM) {
        qDebug() << "contact user item clicked";
        emit sig_switch_apply_friend_page();
        return ;
    }
}
