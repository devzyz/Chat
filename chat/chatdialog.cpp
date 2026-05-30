#include "chatdialog.h"
#include "ui_chatdialog.h"
#include <QAction>
#include <QRandomGenerator>
#include "chatuseritem.h"
#include "loadingdialog.h"
#include <QThread>
#include <QMouseEvent>
#include "tcpmgr.h"
#include "usermgr.h"
#include "chatuseritem.h"
#include "contactuseritem.h"

ChatDialog::ChatDialog(QWidget *parent)
    : QDialog(parent), _mode(ChatUIMode::ChatMode), _state(ChatUIMode::ChatMode)
    , ui(new Ui::ChatDialog), _b_chat_loading(false), _b_contact_loading(false), _cur_chat_uid(0)
{
    ui->setupUi(this);

    // 添加按钮的高亮设置
    ui->add_btn->SetState("normal", "hover", "press");

    // 搜索框最大长度限制
    ui->search_edit->SetMaxLength(15);
    // 搜索框配置左侧的图标和右侧的清除
    QAction *searchAction = new QAction(ui->search_edit);
    searchAction->setIcon(QIcon(":/res/search.png"));
    ui->search_edit->addAction(searchAction, QLineEdit::LeadingPosition);
    // 设置默认文本
    ui->search_edit->setPlaceholderText(QStringLiteral("搜索"));

    QAction *clearAction = new QAction(ui->search_edit);
    clearAction->setIcon(QIcon(":/res/close_transport.png"));
    // 初始时不显示清除图标，将清除动作添加到LineEdit的末尾
    ui->search_edit->addAction(clearAction, QLineEdit::TrailingPosition);

    // 安装事件过滤器，检测鼠标点击位置，清空搜索框
    this->installEventFilter(this);

    // 将头像设置上去
    QPixmap pixmap(":/res/head_1.jpg");
    pixmap = pixmap.scaled(ui->side_head_label->size(), Qt::KeepAspectRatio);
    ui->side_head_label->setPixmap(pixmap);
    ui->side_head_label->setScaledContents(true);

    ui->side_chat_label->SetState("leave", "hover", "select");

    ui->side_user_label->SetState("leave", "hover", "select");

    AddLabelGroup(ui->side_chat_label);
    AddLabelGroup(ui->side_user_label);

    // 添加聊天数据
    addChatUserList();

    // 设置聊天为默认选中界面
    ui->side_chat_label->SetSelected(true);
    // 设置选中条目
    SetSelectChatItem();
    // 设置右侧chatpage页面
    SetSelectChatPage();

    // 默认隐藏
    ShowSearch(false);

    // 将search_edit关联到，search_list中用于搜索逻辑的_search_edit
    ui->search_list->SetSearchEdit(ui->search_edit);

    // 当需要显示搜索框内的清除图标时，更改为实际的清除图标
    connect(ui->search_edit, &QLineEdit::textChanged, [clearAction](const QString& text) {
        if (!text.isEmpty()) {
            clearAction->setIcon(QIcon(":/res/close_search.png"));
        }else {
            clearAction->setIcon(QIcon(":/res/close_transport.png"));
        }
    });

    // 点击清除图标后的逻辑
    // 清空输入框，清除图标删除，失去焦点
    connect(clearAction, &QAction::triggered, [this, clearAction]() {
        ui->search_edit->clear();
        clearAction->setIcon(QIcon(":/res/close_transport.png"));
        ui->search_edit->clearFocus();

        // 隐藏搜索列表
        ShowSearch(false);
    });

    // 连接加载更多聊天列表的信号与槽
    connect(ui->chat_user_list, &ChatUserList::sig_loading_chat_list, this, &ChatDialog::slot_loading_chat_list);

    // 连接加载联系人的信号与槽
    connect(ui->contact_user_list, &ContactUserList::sig_loading_contact_list, this, &ChatDialog::slot_loading_contact_list);

    // 切换当前QListWidget为聊天记录widget
    connect(ui->side_chat_label, &StateWidget::clicked, this, &ChatDialog::slot_midlist_to_chat_list);

    // 切换当前QListWidget为联系人widget
    connect(ui->side_user_label, &StateWidget::clicked, this, &ChatDialog::slot_midlist_to_user_list);

    // 切换当前QListWidget为搜索框，当搜索列表不为空的时候
    connect(ui->search_edit, &QLineEdit::textChanged, this, &ChatDialog::slot_search_edit_text_changed);

    // 连接触发新朋友Page的信号
    connect(ui->contact_user_list, &ContactUserList::sig_switch_apply_friend_list_page,
            this, &ChatDialog::slot_switch_apply_friend_list_page);

    // 连接添加好友申请信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_add_friend_apply,
            this, &ChatDialog::slot_tcp_add_friend_apply);

    // 连接认证添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_add_auth_friend,
            this, &ChatDialog::slot_tcp_add_auth_friend);

    // 连接通知认证添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_notify_auth_friend,
            this, &ChatDialog::slot_tcp_notify_auth_friend);

    // 连接搜索到好友后，跳转到与该好友的聊天界面的信号
    connect(ui->search_list, &SearchList::sig_jump_chat_item, this, &ChatDialog::slot_from_search_jump_chat_item);

    // 在联系人列表，点击联系人后，后侧跳转到对应的联系人信息页面
    connect(ui->contact_user_list, &ContactUserList::sig_switch_friend_info_page,
            this, &ChatDialog::slot_switch_friend_info_page);

    // 在用户详细信息界面，点击聊天后，跳转到聊天页面
    connect(ui->friend_info_page, &FriendInfoPage::sig_jump_chat_item,
            this, &ChatDialog::slot_from_friend_jump_chat_item);

    // 连接聊天列表点击信号
    connect(ui->chat_user_list, &QListWidget::itemClicked, this, &ChatDialog::slot_chat_item_clicked);

    // 连接发送文本信息后，将发送的文本插入到聊天记录中
    connect(ui->chat_page, &ChatPage::sig_append_send_text_chat_msg,
            this, &ChatDialog::slot_append_send_text_chat_msg);

    // 连接服务器通知我添加消息后的信号，将服务器通知的信息刷新到聊天界面上
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_update_text_chat_msg,
            this, &ChatDialog::slot_update_text_chat_msg);
}

ChatDialog::~ChatDialog()
{
    delete ui;
}

/**
 * @brief ChatDialog::addChatUserList
 * 聊天列表
 */
void ChatDialog::addChatUserList()
{
    std::vector<std::shared_ptr<FriendInfo>> chat_list = UserMgr::GetInstance()->GetSomeChatList();
    // 添加聊天列表数据
    if (!chat_list.empty()) {
        for (auto &info : chat_list) {
            auto find_iter = _chat_item_map.find(info->_uid);
            if (find_iter != _chat_item_map.end()) {
                continue;
            }
            // 创建自定义的ChatUserWidget
            auto * chat_user_item = new ChatUserItem();
            chat_user_item->SetInfo(info);
            chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

            // 创建一个能够往QListWidget内部填充的Item
            QListWidgetItem * item = new QListWidgetItem();
            // 将item的大小设置为自定义的widget的大小
            item->setSizeHint(chat_user_item->sizeHint());
            // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
            ui->chat_user_list->addItem(item);
            ui->chat_user_list->setItemWidget(item, chat_user_item);

            _chat_item_map.insert(info->_uid, item);
        }

        UserMgr::GetInstance()->UpdateChatLoadedCount();
    }
}

/**
 * @brief ChatDialog::eventFilter
 * @param watched
 * @param event
 * @return
 * 事件过滤器，实现当处于search状态，并且点击非search list区域时
 * 将search列表切换为上一次显示的列表，并清空搜索框
 */
bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent * mouseEvent = static_cast<QMouseEvent*> (event);
        handleGlobalMousePress(mouseEvent);
    }

    return QDialog::eventFilter(watched, event);
}

/**
 * @brief ChatDialog::ShowSearch
 * @param bsearch
 * 下方有三种形式，聊天列表，搜索列表，联系人列表
 */
void ChatDialog::ShowSearch(bool bsearch)
{
    if (bsearch) {
        ui->chat_user_list->hide();
        ui->contact_user_list->hide();
        ui->search_list->show();
        _mode = ChatUIMode::SearchMode;
    }else if (_state == ChatUIMode::ChatMode) {
        ui->chat_user_list->show();
        ui->contact_user_list->hide();
        ui->search_list->hide();
        _mode = ChatUIMode::ChatMode;
    }else if (_state == ChatUIMode::ContactMode) {
        ui->chat_user_list->hide();
        ui->contact_user_list->show();
        ui->search_list->hide();
        _mode = ChatUIMode::ContactMode;
    }
}

/**
 * @brief ChatDialog::AddLabelGroup
 * @param label
 * 用一个列表存储左侧在同一个组里的StateWdiget组件
 * 因为右边的显示区域只会显示一个
 */
void ChatDialog::AddLabelGroup(StateWidget *label)
{
    _label_list.push_back(label);
}

// 清除其他stateWidget的选中状态，为了保持只有一个被选中
void ChatDialog::ClearLabelState(StateWidget *label)
{
    for (auto & ele : _label_list) {
        if (ele == label) {
            continue;
        }
        ele->ClearState();
    }
}

/**
 * @brief ChatDialog::handleGlobalMousePress
 * @param event
 * 为了处理，当正处在搜索页面的时候，点击其他位置，可以关闭搜索的窗口
 */
void ChatDialog::handleGlobalMousePress(QMouseEvent *event)
{
    // 如果不是搜索模式，则直接返回
    if (_mode != ChatUIMode::SearchMode) {
        return ;
    }

    // 获取相对于整个窗口的坐标，并转换为相对于搜索列表下的坐标
    QPointF posInSearchList = ui->search_list->mapFromGlobal(event->globalPosition());

    // 判断点是不是在搜索列表范围内
    if (!ui->search_list->rect().toRectF().contains(posInSearchList)) {
        // 不在范围内，清空搜索框
        ui->search_edit->clear();
        ShowSearch(false);
    }
}

/**
 * @brief ChatDialog::slot_loading_chat_user
 * 加載更多用戶聊天
 */
void ChatDialog::slot_loading_chat_list()
{
    // 判断当前是否在加载
    if (_b_chat_loading) {
        return ;
    }

    _b_chat_loading = true;
    // 创建一个加载的动画
    LoadingDialog * loadingDialog = new LoadingDialog(this);
    loadingDialog->setModal(true); // 拦截所有的鼠标和键盘事件
    loadingDialog->show();
    // QThread::sleep(2);

    LoadingMoreChat();
    // 加载完毕后关闭对话框
    loadingDialog->deleteLater();

    _b_chat_loading = false;
}

/**
 * @brief ChatDialog::slot_side_chat
 * 点击左侧聊天后，列表切换到聊天记录列表
 */
void ChatDialog::slot_midlist_to_chat_list()
{
    qDebug() << "receive side chat clicked";
    // 传入聊天StateWidget
    ClearLabelState(ui->side_chat_label);
    ui->side_chat_label->ShowRedPoint(false); // 选中后取消红点
    // 设置右面为聊天界面
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    _state = ChatUIMode::ChatMode;
    ShowSearch(false);
}

/**
 * @brief ChatDialog::slot_side_user
 * 点击左侧联系人后，列表切换到联系人列表
 */
void ChatDialog::slot_midlist_to_user_list()
{
    qDebug() << "receive side user clicked";
    ClearLabelState(ui->side_user_label);
    ui->side_user_label->ShowRedPoint(false); // 选中后取消红点
    // 设置右面为好友申请列表
    ui->stackedWidget->setCurrentWidget(ui->apply_friend_page);
    _state = ChatUIMode::ContactMode;
    ShowSearch(false);
}

// 如果搜索框不空，则显示搜索列表
void ChatDialog::slot_search_edit_text_changed(const QString &str)
{
    if (!str.isEmpty()) {
        ShowSearch(true);
    }
}

/**
 * @brief ChatDialog::slot_switch_apply_friend_page
 * 切换到好友申请列表
 */
void ChatDialog::slot_switch_apply_friend_list_page()
{
    qDebug() << "chatdialog : slot switch apply friend list page";
    _apply_friend_page = ui->apply_friend_page;
    ui->stackedWidget->setCurrentWidget(ui->apply_friend_page);
}

/**
 * @brief ChatDialog::slot_tcp_add_auth_friend
 * @param auth_info
 * 接收到认证信号，将对应的聊天item添加上
 */
void ChatDialog::slot_tcp_add_auth_friend(std::shared_ptr<AuthInfo> auth_info)
{
    qDebug() << "chatdialog : slot tcp add auth friend";
    // 创建自定义的ChatUserWidget
    auto * chat_user_item = new ChatUserItem();
    chat_user_item->SetInfo(auth_info);
    chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

    // 创建一个能够往QListWidget内部填充的Item
    QListWidgetItem * item = new QListWidgetItem();
    // 将item的大小设置为自定义的widget的大小
    item->setSizeHint(chat_user_item->sizeHint());
    // 将这个item插入到最上面
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_item);
    _chat_item_map.insert(auth_info->_uid, item);
}

/**
 * @brief ChatDialog::slot_tcp_notify_auth_friend
 * @param auth_info
 * 收到通知认证信号，将聊天记录item添加上
 */
void ChatDialog::slot_tcp_notify_auth_friend(std::shared_ptr<AuthInfo> auth_info)
{
    qDebug() << "chatdialog : slot tcp notify auth friend";
    // 创建自定义的ChatUserWidget
    auto * chat_user_item = new ChatUserItem();
    chat_user_item->SetInfo(auth_info);
    chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

    // 创建一个能够往QListWidget内部填充的Item
    QListWidgetItem * item = new QListWidgetItem();
    // 将item的大小设置为自定义的widget的大小
    item->setSizeHint(chat_user_item->sizeHint());
    // 将这个item插入到最上面
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_item);
    _chat_item_map.insert(auth_info->_uid, item);
}

// 搜索到的人是好友后，跳转到与该好友的聊天界面
void ChatDialog::slot_from_search_jump_chat_item(std::shared_ptr<SearchInfo> si)
{
    qDebug() << "chatdialog : search info -> slot jump chat item";
    // 看看是否已经添加item，如果添加过，则拿出来
    auto find_iter = _chat_item_map.find(si->_uid);
    if (find_iter != _chat_item_map.end()) {
        ui->chat_user_list->scrollToItem(find_iter.value()); // 将列表滚动到用户可以见的viewport区域
        ui->side_chat_label->SetSelected(true); // 选中侧边栏中的聊天
        ui->side_chat_label->ShowRedPoint(false); // 选中后红点消失
        // 设置item为选中状态
        SetSelectChatItem(si->_uid);
        // 更新右侧对应的详细聊天记录
        SetSelectChatPage(si->_uid);
        // 切换真正的list页面
        slot_midlist_to_chat_list();
        return ;
    }

    // 如果没找到，则创建新的QListWidget插入
    auto * chat_user_item = new ChatUserItem();
    auto friend_info = std::make_shared<FriendInfo> (si);
    chat_user_item->SetInfo(friend_info);
    chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

    QListWidgetItem * item = new QListWidgetItem();
    item->setSizeHint(chat_user_item->sizeHint());
    ui->chat_user_list->insertItem(0, item); //插入到顶部
    ui->chat_user_list->setItemWidget(item, chat_user_item);

    // 设置左侧聊天图标为选中状态
    ui->side_chat_label->SetSelected(true);
    ui->side_chat_label->ShowRedPoint(false); // 选中后红点消失
    SetSelectChatItem(si->_uid); // 选中item
    SetSelectChatPage(si->_uid); // 更新右侧聊天页面
    slot_midlist_to_chat_list(); // 更新中间的列表为聊天记录列表
}

// 在好友详细信息界面，点击聊天后跳转到聊天界面
void ChatDialog::slot_from_friend_jump_chat_item(std::shared_ptr<FriendInfo> si)
{
    qDebug() << "chatdialog : user info -> slot jump chat item";
    // 看看是否已经添加item，如果添加过，则拿出来
    auto find_iter = _chat_item_map.find(si->_uid);
    if (find_iter != _chat_item_map.end()) {
        ui->chat_user_list->scrollToItem(find_iter.value()); // 将列表滚动到用户可以见的viewport区域
        ui->side_chat_label->SetSelected(true); // 选中侧边栏中的聊天
        ui->side_chat_label->ShowRedPoint(false); // 选中后，红点消失
        // 设置item为选中状态
        SetSelectChatItem(si->_uid);
        // 更新右侧对应的详细聊天记录
        SetSelectChatPage(si->_uid);
        // 切换真正的list页面
        slot_midlist_to_chat_list();
        return ;
    }

    // 如果没找到，则创建新的QListWidget插入
    auto * chat_user_item = new ChatUserItem();
    auto frined_info = si;
    chat_user_item->SetInfo(frined_info);

    QListWidgetItem * item = new QListWidgetItem();
    item->setSizeHint(chat_user_item->sizeHint());
    ui->chat_user_list->insertItem(0, item); //插入到顶部
    ui->chat_user_list->setItemWidget(item, chat_user_item);

    // 设置左侧聊天图标为选中状态
    ui->side_chat_label->SetSelected(true);
    ui->side_chat_label->ShowRedPoint(false); // 选中后，侧边栏红点消失
    SetSelectChatItem(si->_uid); // 选中item
    SetSelectChatPage(si->_uid); // 更新右侧聊天页面
    slot_midlist_to_chat_list(); // 更新中间的列表为聊天记录列表
}

// 右侧跳转到好友详细信息界面
void ChatDialog::slot_switch_friend_info_page(std::shared_ptr<FriendInfo> friend_info)
{
    qDebug() << "chatdialog : slot switch friend info page";
    ui->stackedWidget->setCurrentWidget(ui->friend_info_page);
    ui->friend_info_page->SetInfo(friend_info);
}

// 当聊天列表的item被点击后，触发的槽函数
void ChatDialog::slot_chat_item_clicked(QListWidgetItem * item)
{
    // 获取到这个item内部绑定的自定义item
    QWidget *widget = ui->chat_user_list->itemWidget(item);
    if (!widget) {
        qDebug() << "slot chat item clicked widget is nullptr";
        return ;
    }

    // 转成通用的基类
    ListItemBase * itembase = qobject_cast<ListItemBase*> (widget);
    if (!itembase) {
        qDebug() << "slot item clicked widget is nullptr";
        return ;
    }

    // 根据内部的itemtype转成对应的类型
    auto itemType = itembase->GetItemType();
    if (itemType == ListItemType::INVALID_ITEM ||
        itemType == ListItemType::GROUP_TIP_ITEM) {
        qDebug() << "slot invalid item clicked";
        return ;
    }

    // 如果是聊天类型，则进行转换
    if (itemType == ListItemType::CHAT_USER_ITEM) {
        qDebug() << "chat user item clicked";

        auto chat_item = qobject_cast<ChatUserItem*> (itembase);
        auto chat_info = chat_item->GetChatInfo();
        chat_item->ResetNewMsgCount(); // 被点击后，重置红点的刷新

        // 设置右侧的聊天界面
        ui->chat_page->SetChatInfo(chat_info);
        _cur_chat_uid = chat_info->_uid;
    }
}

// 将发送的文本插入到聊天记录中
void ChatDialog::slot_append_send_text_chat_msg(std::shared_ptr<TextChatData> text_chat_data)
{
    qDebug() <<"chat dialog : slot append send text chat msg";
    int chat_uid = text_chat_data->_to_uid;
    // 如果当前uid <= 0代表非法
    if (chat_uid <= 0) {
        return ;
    }

    // 找不到对应的item
    auto find_iter = _chat_item_map.find(chat_uid);
    if (find_iter == _chat_item_map.end()) {
        return ;
    }

    // 拿到item内部绑定的自定义item
    QWidget * widget = ui->chat_user_list->itemWidget(find_iter.value());
    if (!widget) {
        return ;
    }

    // 转换为基类的item
    ListItemBase * baseItem = qobject_cast<ListItemBase*> (widget);
    if (!baseItem) {
        return ;
    }

    // 如果当前是聊天的item
    auto itemType = baseItem->GetItemType();
    if (itemType == ListItemType::CHAT_USER_ITEM) {
        auto * chat_item = qobject_cast<ChatUserItem*> (baseItem);
        if (!chat_item) {
            return ;
        }

        // 将发送的信息放入聊天记录中
        auto chat_info = chat_item->GetChatInfo();
        chat_info->_chat_msgs.push_back(text_chat_data);

        // 设置上一次聊天记录
        chat_item->SetLastTextChatMsg(text_chat_data->_msg_content);
        chat_info->_last_msg = text_chat_data->_msg_content;

        return ;
    }
}

// 将服务器通知的信息，刷新到界面上
void ChatDialog::slot_update_text_chat_msg(int from_uid, int to_uid, QJsonArray text_array)
{
    // 如果不在聊天界面，则将聊天界面红点显示出来
    auto _side_chat_label_isSelect = ui->side_chat_label->GetCurState();
    if (_side_chat_label_isSelect == ClickLabelState::Normal) {
        ui->side_chat_label->ShowRedPoint(true);
    }

    // 如果当前正在聊天的人不是发送信息的人，则更新红点，并返回
    if (_cur_chat_uid != from_uid) {
        // 他发送新消息，不管当前在哪个页面，都把红点显示出来
        // 找到发送来的那个人，将他的红点显示出来
        auto find_iter = _chat_item_map.find(from_uid);
        if (find_iter == _chat_item_map.end()) {
            return ;
        }

        // 拿到item内部绑定的自定义item
        QWidget * widget = ui->chat_user_list->itemWidget(find_iter.value());
        if (!widget) {
            return ;
        }

        // 转换为基类的item
        ListItemBase * baseItem = qobject_cast<ListItemBase*> (widget);
        if (!baseItem) {
            return ;
        }

        // 如果当前是聊天的item
        auto itemType = baseItem->GetItemType();
        if (itemType == ListItemType::CHAT_USER_ITEM) {
            auto * chat_item = qobject_cast<ChatUserItem*> (baseItem);
            if (!chat_item) {
                return ;
            }

            // 更新红点
            chat_item->UpdateNewMsgCount(text_array.count());
            return ;
        }
        return ;
    }

    for (const auto& msg : text_array) {
        QJsonObject obj = msg.toObject();
        auto msg_id = obj["msg_id"].toString();
        auto msg_content = obj["msg_content"].toString();
        auto text_msg = std::make_shared<TextChatData> (from_uid, to_uid, msg_id, msg_content);
        ui->chat_page->AppendChatMsg(text_msg);
    }
}

// 选中当前正在聊天的item
void ChatDialog::SetSelectChatItem(int uid) {
    // 如果没有item则返回
    if (ui->chat_user_list->count() <= 0) {
        return ;
    }

    // 如果uid非法，uid <= 0 或者 在item列表中找不到，则设置当前选中的为第0行
    // 并设置当前的uid为第0行对应的item的uid
    auto iter_find = _chat_item_map.find(uid);
    if (uid <= 0 || iter_find == _chat_item_map.end()) {
        qDebug() << "set uid : " << uid << " failure, set current row 0";
        ui->chat_user_list->setCurrentRow(0);

        // 设置_cur_chat_uid为第0行的
        QListWidgetItem * firstItem = ui->chat_user_list->item(0);
        if (!firstItem) {
            return ;
        }
        // 取到item内部放入的自定义tiem
        QWidget * widget = ui->chat_user_list->itemWidget(firstItem);
        if (!widget) {
            return ;
        }

        auto chatListItem = qobject_cast<ChatUserItem*>(widget);
        if (!chatListItem) {
            return;
        }
        chatListItem->ResetNewMsgCount(); // 选中后，将新消息提醒关闭
        _cur_chat_uid = chatListItem->GetChatInfo()->_uid;

        return ;
    }

    ui->chat_user_list->setCurrentItem(iter_find.value());

    _cur_chat_uid = uid;

    // 取消红点显示
    QWidget * widget = ui->chat_user_list->itemWidget(iter_find.value());
    if (!widget) {
        return ;
    }

    auto chatListItem = qobject_cast<ChatUserItem*>(widget);
    if (!chatListItem) {
        return;
    }
    chatListItem->ResetNewMsgCount(); // 选中后，将新消息提醒关闭
}

// 设置右侧详细聊天记录界面
void ChatDialog::SetSelectChatPage(int uid) {
    // 如果没有则返回
    if (ui->chat_user_list->count() <= 0) {
        return ;
    }

    // 如果uid非法，uid <= 0 或者 在item列表中找不到，则设置当前选中的为第0行
    // 并设置当前的uid为第0行对应的item的uid
    auto iter_find = _chat_item_map.find(uid);
    QListWidgetItem * _item = nullptr;
    if (uid <= 0 || iter_find == _chat_item_map.end()) {
        // 设置_cur_chat_uid为第0行的
        _item = ui->chat_user_list->item(0);
    }else {
        // 设置为找到的uid
        _item = iter_find.value();
    }

    if (!_item) {
        return ;
    }
    // 取到item内部放入的自定义tiem
    QWidget * widget = ui->chat_user_list->itemWidget(_item);
    if (!widget) {
        return ;
    }

    auto chatListItem = qobject_cast<ChatUserItem*>(widget);
    if (!chatListItem) {
        return;
    }

    // 设置信息
    auto chat_info = chatListItem->GetChatInfo();
    ui->chat_page->SetChatInfo(chat_info);
}

// 加载更多聊天记录
void ChatDialog::LoadingMoreChat() {
    std::vector<std::shared_ptr<FriendInfo>> chat_list = UserMgr::GetInstance()->GetSomeChatList();
    // 添加聊天列表数据
    if (!chat_list.empty()) {
        for (auto &info : chat_list) {
            auto find_iter = _chat_item_map.find(info->_uid);
            if (find_iter != _chat_item_map.end()) {
                continue;
            }
            // 创建自定义的ChatUserWidget
            auto * chat_user_item = new ChatUserItem();
            chat_user_item->SetInfo(info);
            chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

            // 创建一个能够往QListWidget内部填充的Item
            QListWidgetItem * item = new QListWidgetItem();
            // 将item的大小设置为自定义的widget的大小
            item->setSizeHint(chat_user_item->sizeHint());
            // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
            ui->chat_user_list->addItem(item);
            ui->chat_user_list->setItemWidget(item, chat_user_item);

            _chat_item_map.insert(info->_uid, item);
        }
        // 更新现在已经加载的数据
        UserMgr::GetInstance()->UpdateChatLoadedCount();
    }
}

// 加载更多联系人
void ChatDialog::LoadingMoreContact() {
    auto contact_list = UserMgr::GetInstance()->GetSomeContactList();
    if (!contact_list.empty()) {
        for (auto &info : contact_list) {
            auto contact_user_item = new ContactUserItem();
            contact_user_item->SetInfo(info);
            contact_user_item->SetItemType(ListItemType::CONTACT_USER_ITEM);

            QListWidgetItem * contact_item = new QListWidgetItem();
            contact_item->setSizeHint(contact_user_item->sizeHint());
            ui->contact_user_list->addItem(contact_item);
            ui->contact_user_list->setItemWidget(contact_item, contact_user_item);
        }
        // 更新现在已经加载的数据
        UserMgr::GetInstance()->UpdateContactLoadedCount();
    }
}

// 加载更多联系人列表槽函数
void ChatDialog::slot_loading_contact_list()
{
    qDebug() << "chatdialog : slot loading contact list";

    // 判断当前是否在加载
    if (_b_contact_loading) {
        return ;
    }

    _b_contact_loading = true;
    // 创建一个加载的动画
    LoadingDialog * loadingDialog = new LoadingDialog(this);
    loadingDialog->setModal(true); // 拦截所有的鼠标和键盘事件
    loadingDialog->show();
    // QThread::sleep(2);

    LoadingMoreContact();
    // 加载完毕后关闭对话框
    loadingDialog->deleteLater();

    _b_contact_loading = false;
}

void ChatDialog::slot_tcp_add_friend_apply(std::shared_ptr<ApplyInfo> applyInfo)
{
    qDebug() << "chatdialog : slot tcp add friend apply";

    // 先判断是否已经添加过请求
    int b_already_apply = UserMgr::GetInstance()->AlreadyApplyAddFriend(applyInfo->_uid);
    if (b_already_apply) {
        qDebug() << "already apply";
        return ;
    }

    // 插入请求添加好友列表
    UserMgr::GetInstance()->AddApply(applyInfo->_uid, applyInfo);

    // 当前选中的不是联系人处后，添加红点提示
    if (ui->side_chat_label->GetCurState() == ClickLabelState::Normal) {
        // 展示左侧联系人处的红点提醒
        ui->side_user_label->ShowRedPoint(true);
    }
    // 设置新的朋友item处的红点提醒
    ui->contact_user_list->ShowRedPoint(true);
    // 将新的请求插入到列表中
    ui->apply_friend_page->AddNewApply(applyInfo);
}

