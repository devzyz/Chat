#include "searchlist.h"
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include "tcpmgr.h"
#include "adduseritem.h"
#include "findsuccessdialog.h"

SearchList::SearchList(QWidget * parent)
    : QListWidget(parent) , _find_dialog(nullptr), _search_edit(nullptr), _send_pending(false) {
    Q_UNUSED(parent);

    // 关闭滚动条
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 安装事件过滤器
    this->viewport()->installEventFilter(this);
    // 连接点击信号和槽
    connect(this, &QListWidget::itemClicked, this, &SearchList::slot_item_clicked);

    // 添加条目
    addTipItem();

    // 连接搜索条目
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_user_search, this, &SearchList::slot_user_search);
}

void SearchList::CloseFindDialog()
{
    if (_find_dialog) {
        _find_dialog->hide(); // 隐藏如果其他地方没有使用，则会析构
        _find_dialog = nullptr;
    }
}

void SearchList::SetSearchEdit(QWidget *edit)
{

}

bool SearchList::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->viewport()) {
        // 鼠标进入事件与离开事件
        if (event->type() == QEvent::Enter) {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }else if (event->type() == QEvent::Leave) {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 检查事件是否是鼠标滚轮事件
    if (watched == this->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent * wheelEvent = static_cast<QWheelEvent * > (event);
        int numDegrees = wheelEvent->angleDelta().y() / 8;
        int numSteps = numDegrees / 15;

        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        return true;
    }

    return QListWidget::eventFilter(watched, event);
}

void SearchList::waitPending(bool pending)
{

}

// 添加测试提示
void SearchList::addTipItem()
{
    auto *invalid_item = new QWidget();
    QListWidgetItem *item_tmp = new QListWidgetItem();
    item_tmp->setSizeHint(QSize(250,10));
    this->addItem(item_tmp);
    invalid_item->setObjectName("invalid_item");
    this->setItemWidget(item_tmp, invalid_item);
    item_tmp->setFlags(item_tmp->flags() & ~Qt::ItemIsSelectable);


    auto *add_user_item = new AddUserItem();
    QListWidgetItem *item = new QListWidgetItem();
    item->setSizeHint(add_user_item->sizeHint());
    this->addItem(item);
    this->setItemWidget(item, add_user_item);
}

// 当某个搜索到的条目被点击时触发
void SearchList::slot_item_clicked(QListWidgetItem *item)
{
    // 获取自定义的widget对象
    QWidget * widget = this->itemWidget(item);
    if (!widget) {
        qDebug() << "slot item clicked widget is nullptr";
        return ;
    }

    // 自定义了很多item，先将item转换为基类的
    ListItemBase * customItem = qobject_cast<ListItemBase * > (widget);
    if (!customItem) {
        qDebug() << "slot item clicked widget is nullptr";
        return ;
    }

    // 判断type是不是invalid_item
    auto itemType = customItem->GetItemType();
    if (itemType == ListItemType::INVALID_ITEM) {
        qDebug() << "slot invalid itme clicked";
        return ;
    }

    // 当点击的是添加用户的item
    if (itemType == ListItemType::ADD_USER_TIP_ITEM) {
        // todo...
        _find_dialog = std::make_shared<FindSuccessDialog> (this);
        auto si = std::make_shared<SearchInfo> (0, "zyz", "zyz", "hello , my friend", 0);
        std::dynamic_pointer_cast<FindSuccessDialog> (_find_dialog)->SetSearchInfo(si);
        _find_dialog->show();
        return ;
    }

    //清除弹出框
    CloseFindDialog();
}

void SearchList::slot_user_search(std::shared_ptr<SearchInfo> si)
{

}
