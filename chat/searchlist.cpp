#include "searchlist.h"
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include "tcpmgr.h"
#include "adduseritem.h"
#include "findsuccessdialog.h"
#include "customizeedit.h"
#include <QJsonDocument>
#include "findfaildialog.h"
#include "usermgr.h"

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
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_search_user_finish, this, &SearchList::slot_tcp_search_user_finish);
}

void SearchList::CloseFindDialog()
{
    if (_find_dialog) {
        _find_dialog->hide(); // 隐藏如果其他地方没有使用，则会析构
        _find_dialog = nullptr;
    }
}

/**
 * @brief SearchList::SetSearchEdit
 * @param edit
 * 设置当前搜索框
 */
void SearchList::SetSearchEdit(QWidget *edit)
{
    _search_edit = edit;
}

/**
 * @brief SearchList::eventFilter
 * @param watched
 * @param event
 * @return
 * 处理鼠标滚轮事件
 */
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

/**
 * @brief SearchList::waitPending
 * @param pending
 * 因为发送网络请求可能要时间，通过这个创建一个等待
 */
void SearchList::waitPending(bool pending)
{
    if (pending) {
        _loadingDialog = new LoadingDialog(this);
        _loadingDialog->setModal(true);
        _loadingDialog->show();
        _send_pending = true;
    }else {
        _loadingDialog->hide();
        _loadingDialog->deleteLater();
        _send_pending = false;
    }
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
        // 用一个变量来控制当前是否在查找
        if (_send_pending) {
            return ;
        }

        if (!_search_edit) {
            return ;
        }

        // 添加一个正在等待的函数
        waitPending(true);
        // 准备发送tcp请求
        auto search_edit = dynamic_cast<CustomizeEdit*> (_search_edit);
        // 支持通过uid/name两种方式，当输入全为数字，判定为uid，否则判定为name
        auto uid_name = search_edit->text();
        QJsonObject jsonObj;
        jsonObj["uid_name"] = uid_name;
        // 将json数据转换为字节流数据
        QJsonDocument doc(jsonObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_SEARCH_USER_REQ, jsonData);

        return ;
    }

    //清除弹出框
    CloseFindDialog();
}

/**
 * @brief SearchList::slot_search_user_finish
 * @param si
 * 搜索用于的tcp请求结束
 */
void SearchList::slot_tcp_search_user_finish(std::shared_ptr<SearchInfo> si)
{
    // 网络请求结束，停止等待
    waitPending(false);
    if (si == nullptr) {
        _find_dialog = std::make_shared<FindFailDialog> (this);
    }else {
        // 搜索到用户，存在三种逻辑，一不是我的好友，二是我的好友，三是我自己
        // 是我自己, 直接返回，不做处理
        auto self_uid = UserMgr::GetInstance()->GetUid();
        if (si->_uid == self_uid) {
            return ;
        }

        // 是我的好友逻辑，则直接跳转到聊天界面
        auto isFriend = UserMgr::GetInstance()->CheckIsFriendById(si->_uid);
        if (isFriend) {
            emit sig_jump_chat_item(si);
            return ;
        }

        // 不是我的好友逻辑
        _find_dialog = std::make_shared<FindSuccessDialog> (this);
        // 设置一下搜索成功的弹出框的信息
        std::dynamic_pointer_cast<FindSuccessDialog>(_find_dialog)->SetSearchInfo(si);
    }

    _find_dialog->show();
}
