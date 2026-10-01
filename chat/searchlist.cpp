#include "searchlist.h"
#include "listviewbehavior.h"
#include "logmgr.h"
#include <QEvent>
#include "tcpmgr.h"
#include "adduseritem.h"
#include "findsuccessdialog.h"
#include "customizeedit.h"
#include <QJsonDocument>
#include "findfaildialog.h"
#include "usermgr.h"

SearchList::SearchList(QWidget * parent)
    : QListWidget(parent) , _find_dialog(nullptr), _send_pending(false) {
    Q_UNUSED(parent);

    new ListViewBehavior(this);
    // 连接点击信号和槽
    connect(this, &QListWidget::itemClicked, this, &SearchList::itemClicked);

    // 添加条目
    addTipItem();

    // 连接搜索条目

}

void SearchList::closeFindDialog()
{
    if (_find_dialog) {
        _find_dialog->hide();
        _find_dialog->deleteLater();
        _find_dialog = nullptr;
    }
}

/**
 * @brief SearchList::waitPending
 * @param pending
 * 因为发送网络请求可能要时间，通过这个创建一个等待
 */
void SearchList::waitPending(bool pending)
{
    if (_send_pending == pending) return;
    _send_pending = pending;
    if (pending) {
        _loadingDialog = new LoadingDialog(this);
        connect(_loadingDialog, &QDialog::rejected, this, &SearchList::cancelSearch);
        _loadingDialog->show();
    } else if (_loadingDialog) {
        _loadingDialog->hide(); _loadingDialog->deleteLater(); _loadingDialog.clear();
    }
}
void SearchList::cancelSearch()
{
    emit searchCancelled();
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
void SearchList::itemClicked(QListWidgetItem *item)
{
    // 获取自定义的widget对象
    QWidget * widget = this->itemWidget(item);
    if (!widget) {
        SPDLOG_WARN("clicked search list widget is null");
        return ;
    }

    // 自定义了很多item，先将item转换为基类的
    ListItemBase * customItem = qobject_cast<ListItemBase * > (widget);
    if (!customItem) {
        SPDLOG_WARN("clicked search list item is null");
        return ;
    }

    // 判断type是不是invalid_item
    auto itemType = customItem->getItemType();
    if (itemType == ListItemType::INVALID_ITEM) {
        SPDLOG_WARN("invalid search list item clicked");
        return ;
    }

    // 当点击的是添加用户的item
    if (itemType == ListItemType::ADD_USER_TIP_ITEM) {
        // 用一个变量来控制当前是否在查找
        if (_send_pending) {
            return ;
        }

        emit searchRequested();

        return ;
    }

    //清除弹出框
    closeFindDialog();
}

/**
 * @brief SearchList::slot_search_user_finish
 * @param si
 * 搜索用于的tcp请求结束
 */
void SearchList::tcpSearchUserFinish(std::shared_ptr<SearchInfo> si)
{
    // 网络请求结束，停止等待
    closeFindDialog();
    if (si == nullptr) {
        _find_dialog = new FindFailDialog(this);
    }else {
        // 搜索到用户，存在三种逻辑，一不是我的好友，二是我的好友，三是我自己
        // 是我自己, 直接返回，不做处理
        auto self_uid = UserMgr::instance()->uid();
        if (si->_uid == self_uid) {
            return ;
        }

        // 是我的好友逻辑，则直接跳转到聊天界面
        auto isFriend = UserMgr::instance()->isFriend(si->_uid);
        if (isFriend) {
            emit chatRequested(si);
            return ;
        }

        // 不是我的好友逻辑
        _find_dialog = new FindSuccessDialog(this);
        // 设置一下搜索成功的弹出框的信息
        qobject_cast<FindSuccessDialog*>(_find_dialog.data())->setSearchInfo(si);
    }

    _find_dialog->show();
}
