#include "chatdialog.h"
#include "logmgr.h"
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
#include <QTimer>
#include <QJsonDocument>
#include <QByteArray>

ChatDialog::ChatDialog(QWidget *parent)
    : QDialog(parent), _mode(ChatUIMode::ChatMode), _state(ChatUIMode::ChatMode)
    , ui(new Ui::ChatDialog), _b_chat_loading(false), _b_contact_loading(false), _cur_chat_id(0)
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

    // 设置左侧菜单栏状态
    ui->side_chat_label->SetState("leave", "hover", "select");
    ui->side_user_label->SetState("leave", "hover", "select");
    ui->side_setting_label->SetState("leave", "hover", "select");

    AddLabelGroup(ui->side_chat_label);
    AddLabelGroup(ui->side_user_label);
    AddLabelGroup(ui->side_setting_label);

    // 设置聊天为默认选中界面
    ui->side_chat_label->SetSelected(true);

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

    // 切换右侧界面为Setting界面
    connect(ui->side_setting_label, &StateWidget::clicked, this, &ChatDialog::slot_switch_user_info_page);

    // 切换当前QListWidget为搜索框，当搜索列表不为空的时候
    connect(ui->search_edit, &QLineEdit::textChanged, this, &ChatDialog::slot_search_edit_text_changed);

    // 连接触发新朋友Page的信号
    connect(ui->contact_user_list, &ContactUserList::sig_switch_apply_friend_list_page,
            this, &ChatDialog::slot_switch_apply_friend_list_page);

    // 连接添加好友申请信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_add_friend_apply,
            this, &ChatDialog::slot_tcp_add_friend_apply);

    // 连接认证添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_add_auth_chat_list,
            this, &ChatDialog::slot_tcp_add_chat_list);

    // 连接搜索到好友后，跳转到与该好友的聊天界面的信号
    connect(ui->search_list, &SearchList::sig_jump_chat_item, this, &ChatDialog::slot_from_search_jump_chat_item);

    // 在联系人列表，点击联系人后，右侧跳转到对应的联系人信息页面
    connect(ui->contact_user_list, &ContactUserList::sig_switch_friend_info_page,
            this, &ChatDialog::slot_switch_friend_info_page);

    // 在用户详细信息界面，点击聊天后，跳转到聊天页面
    connect(ui->friend_info_page, &FriendInfoPage::sig_jump_chat_item,
            this, &ChatDialog::slot_from_friend_jump_chat_item);

    // 连接聊天列表点击信号
    connect(ui->chat_user_list, &QListWidget::itemClicked, this, &ChatDialog::slot_chat_item_clicked);

    // 连接发送文本信息后，将发送的文本插入到聊天记录中
    connect(ui->chat_page, &ChatPage::sig_append_send_text_cache_msg,
            this, &ChatDialog::slot_append_send_text_cache_msg);
    connect(ui->chat_page, &ChatPage::sig_request_history,
            this, &ChatDialog::TcpLoadingMoreChatMsg);

    // 连接服务器通知我添加消息后的信号，将服务器通知的信息刷新到聊天界面上
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_update_text_chat_msg,
            this, &ChatDialog::slot_update_text_chat_msg);

    // 心跳检测定时器
    _timer = new QTimer(this);

    // 连接心跳检测定时器信号
    connect(_timer, &QTimer::timeout, this, [this]() {
        auto user_info = UserMgr::GetInstance()->GetUserInfo();
        QJsonObject jsonObj;
        jsonObj["uid"] = user_info->_uid;
        QJsonDocument doc(jsonObj);
        QByteArray data = doc.toJson(QJsonDocument::Compact); // 转换为字节流，按照压缩方式
        emit TcpMgr::GetInstance()->sig_send_data(ID_HEART_BEAT_REQ, data);
    });

    // 每10秒触发一次
    _timer->start(10000);

    // 连接增量加载聊天列表完成
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_load_chat_finish, this, &ChatDialog::slot_tcp_load_chat_finish);

    // 连接创建私聊请求完成函数
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_create_private_chat_finish,
            this, &ChatDialog::slot_create_private_chat_finish);

    // 连接增量加载聊天记录完成
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_load_chat_msg_finish,
            this, &ChatDialog::slot_tcp_loading_more_chat_finish);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_load_chat_msg_failed,
            this, &ChatDialog::slot_tcp_loading_more_chat_failed);

    // 连接服务器回包之后的状态更新
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_chat_msg_rsp_finish,
            this, &ChatDialog::slot_text_chat_msg_rsp_finish);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_chat_msg_failed,
            this, &ChatDialog::slot_text_chat_msg_failed);
}

ChatDialog::~ChatDialog()
{
    delete ui;
}

// 获取一部分聊天列表
void ChatDialog::LoadChatUesrList()
{
    QJsonObject jsonObj;
    jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    jsonObj["current_chat_id"] = UserMgr::GetInstance()->GetCurrentLoadChatId();

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 请求加载一部分chat_id
    TcpMgr::GetInstance()->sig_send_data(ID_LOAD_CHAT_LIST_REQ, jsonData);
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
 * 加載更多用戶聊天列表
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

    LoadChatUesrList();
    // 加载完毕后关闭对话框
    loadingDialog->deleteLater();
}

/**
 * @brief ChatDialog::slot_side_chat
 * 点击左侧聊天后，列表切换到聊天记录列表
 */
void ChatDialog::slot_midlist_to_chat_list()
{
    SPDLOG_DEBUG("chat navigation selected");
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
    SPDLOG_DEBUG("contacts navigation selected");
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
    SPDLOG_DEBUG("switching to friend application page");
    ui->stackedWidget->setCurrentWidget(ui->apply_friend_page);
}

/**
 * @brief ChatDialog::AddNewChat
 * @param chat_info
 * 添加新的会话到聊天列表中
 */
void ChatDialog::AddNewChat(std::shared_ptr<ChatInfo> chat_info) {
    // 创建自定义的ChatUserWidget
    auto * chat_user_item = new ChatUserItem();
    chat_user_item->SetChatInfo(chat_info);

    // 创建一个能够往QListWidget内部填充的Item
    QListWidgetItem * item = new QListWidgetItem();
    // 将item的大小设置为自定义的widget的大小
    item->setSizeHint(chat_user_item->sizeHint());
    // 将这个item插入到最上面
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_item);
    // key 为chat_id
    _chat_item_map.insert(chat_info->GetChatId(), item);
}

/**
 * @brief ChatDialog::slot_tcp_add_auth_friend
 * @param auth_info
 * 认证回包，或者服务器通知回包
 */
void ChatDialog::slot_tcp_add_chat_list(std::shared_ptr<ChatInfo> chat_info)
{
    SPDLOG_DEBUG("authenticated friend received from TCP");
    AddNewChat(chat_info);
}

// 搜索到的人是好友后，跳转到与该好友的聊天界面
void ChatDialog::slot_from_search_jump_chat_item(std::shared_ptr<SearchInfo> si)
{
    SPDLOG_DEBUG("opening chat from search result");
    auto chat_id = UserMgr::GetInstance()->GetUidToChatId(si->_uid);

    if (chat_id == -1) {
        QJsonObject json;
        json["other_uid"] = si->_uid;
        json["other_name"] = si->_name;
        json["other_description"] = si->_description;
        json["other_icon"] = si->_icon;
        json["other_sex"] = si->_sex;
        LoadOncePrivateChat(UserMgr::GetInstance()->GetUid(), si->_uid, json);
        return;
    }

    chat_id = UserMgr::GetInstance()->GetUidToChatId(si->_uid);

    // 取出他对应的item
    auto find_iter = _chat_item_map.find(chat_id);
    if (find_iter == _chat_item_map.end()) {
        SPDLOG_WARN("chat item not found for search result");
        return;
    }
    ui->chat_user_list->scrollToItem(find_iter.value()); // 将列表滚动到用户可以见的viewport区域
    ui->side_chat_label->SetSelected(true); // 选中侧边栏中的聊天
    ui->side_chat_label->ShowRedPoint(false); // 选中后，红点消失
    // 设置item为选中状态
    SetSelectChatItem(si->_uid);
    // 更新右侧对应的详细聊天记录
    SetSelectChatPage(si->_uid);
    // 切换真正的list页面
    slot_midlist_to_chat_list();
}

void ChatDialog::slot_create_private_chat_finish(std::shared_ptr<ChatInfo> chat_info) {
    // 创建信息进行插入
    auto * chat_user_item = new ChatUserItem();
    chat_user_item->SetChatInfo(chat_info);
    chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

    QListWidgetItem * item = new QListWidgetItem();
    item->setSizeHint(chat_user_item->sizeHint());
    ui->chat_user_list->insertItem(0, item); //插入到顶部
    ui->chat_user_list->setItemWidget(item, chat_user_item);

    _chat_item_map.insert(chat_info->GetChatId(), item);

    ui->chat_user_list->scrollToItem(item); // 将列表滚动到用户可以见的viewport区域
    ui->side_chat_label->SetSelected(true); // 选中侧边栏中的聊天
    ui->side_chat_label->ShowRedPoint(false); // 选中后，红点消失
    // 设置item为选中状态
    SetSelectChatItem(chat_info->GetUid());
    // 更新右侧对应的详细聊天记录
    SetSelectChatPage(chat_info->GetUid());
    // 切换真正的list页面
    slot_midlist_to_chat_list();
}

// 在好友详细信息界面，点击聊天后跳转到聊天界面
void ChatDialog::slot_from_friend_jump_chat_item(std::shared_ptr<UserInfo> si)
{
    SPDLOG_DEBUG("opening chat from user information");
    auto chat_id = UserMgr::GetInstance()->GetUidToChatId(si->_uid);

    if (chat_id == -1) {
        QJsonObject json;
        json["other_uid"] = si->_uid;
        json["other_name"] = si->_name;
        json["other_description"] = si->_description;
        json["other_icon"] = si->_icon;
        json["other_sex"] = si->_sex;
        LoadOncePrivateChat(UserMgr::GetInstance()->GetUid(), si->_uid, json);
        return;
    }

    chat_id = UserMgr::GetInstance()->GetUidToChatId(si->_uid);

    // 取出他对应的item
    auto find_iter = _chat_item_map.find(chat_id);
    if (find_iter == _chat_item_map.end()) {
        SPDLOG_WARN("chat item not found for user information");
        return;
    }
    ui->chat_user_list->scrollToItem(find_iter.value()); // 将列表滚动到用户可以见的viewport区域
    ui->side_chat_label->SetSelected(true); // 选中侧边栏中的聊天
    ui->side_chat_label->ShowRedPoint(false); // 选中后，红点消失
    // 设置item为选中状态
    SetSelectChatItem(si->_uid);
    // 更新右侧对应的详细聊天记录
    SetSelectChatPage(si->_uid);
    // 切换真正的list页面
    slot_midlist_to_chat_list();
}

void ChatDialog::LoadOncePrivateChat(int self_id, int other_id, QJsonObject json)
{
    QJsonObject obj;
    obj["self_id"] = self_id;
    obj["other_id"] = other_id;
    obj["other_info"] = json;

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    emit TcpMgr::GetInstance()->sig_send_data(ID_CREATE_PRIVATE_CHAT_REQ, data);
}


// 右侧跳转到好友详细信息界面
void ChatDialog::slot_switch_friend_info_page(std::shared_ptr<UserInfo> friend_info)
{
    SPDLOG_DEBUG("switching to friend information page");
    ui->stackedWidget->setCurrentWidget(ui->friend_info_page);
    ui->friend_info_page->SetInfo(friend_info);
}

// 当聊天列表的item被点击后，触发的槽函数
void ChatDialog::slot_chat_item_clicked(QListWidgetItem * item)
{
    // 获取到这个item内部绑定的自定义item
    QWidget *widget = ui->chat_user_list->itemWidget(item);
    if (!widget) {
        SPDLOG_WARN("clicked chat list widget is null");
        return ;
    }

    // 转成通用的基类
    ListItemBase * itembase = qobject_cast<ListItemBase*> (widget);
    if (!itembase) {
        SPDLOG_WARN("clicked chat list item is null");
        return ;
    }

    // 根据内部的itemtype转成对应的类型
    auto itemType = itembase->GetItemType();
    if (itemType == ListItemType::INVALID_ITEM ||
        itemType == ListItemType::GROUP_TIP_ITEM) {
        SPDLOG_WARN("invalid chat list item clicked");
        return ;
    }

    // 如果是聊天类型，则进行转换
    if (itemType == ListItemType::CHAT_USER_ITEM) {
        SPDLOG_DEBUG("chat user item clicked");

        auto chat_item = qobject_cast<ChatUserItem*> (itembase);
        auto chat_info = chat_item->GetChatInfo();
        chat_item->ResetNewMsgCount(); // 被点击后，重置红点的刷新

        _cur_chat_id = chat_info->GetChatId();
        // 设置右侧的聊天界面
        ui->chat_page->SetChatInfo(chat_info);
        _cur_chat_id = chat_info->GetChatId();
    }
}

// 将发送的文本插入到聊天缓存中
void ChatDialog::slot_append_send_text_cache_msg(QString uuid, std::shared_ptr<ChatDataBase> text_chat_data)
{
    SPDLOG_DEBUG("appending outgoing text chat message");
    int chat_id = text_chat_data->GetChatId();
    // 找不到对应的item
    auto find_iter = _chat_item_map.find(chat_id);
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
        chat_info->AddCacheChatData(uuid, text_chat_data);

        return ;
    }
}

// 将服务器通知的信息，刷新到界面上
void ChatDialog::slot_update_text_chat_msg(int from_uid, int to_uid, int chat_id, std::vector<std::shared_ptr<ChatDataBase>>& msgs)
{
    Q_UNUSED(from_uid);
    Q_UNUSED(to_uid);
    // 始终写入 chatId 对应的常驻 Model；非当前会话不会操作当前 View。
    for (const auto &msg : msgs) {
        ui->chat_page->AppendChatMsg(msg);
    }

    // 如果不在聊天界面，则将聊天界面红点显示出来
    auto _side_chat_label_isSelect = ui->side_chat_label->GetCurState();
    if (_side_chat_label_isSelect == ClickLabelState::Normal) {
        ui->side_chat_label->ShowRedPoint(true);
    }

    // 如果当前正在聊天的人不是发送信息的人，则更新红点，并返回
    if (_cur_chat_id != chat_id) {
        // 他发送新消息，不管当前在哪个页面，都把红点显示出来
        // 找到发送来的那个人，将他的红点显示出来
        auto find_iter = _chat_item_map.find(chat_id);
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
            chat_item->UpdateNewMsgCount(msgs.size());
            return ;
        }
        return ;
    }
}

void ChatDialog::slot_switch_user_info_page()
{
    SPDLOG_DEBUG("settings navigation selected");
    SPDLOG_DEBUG("switching to settings page");
    // 传入聊天StateWidget
    ClearLabelState(ui->side_setting_label);
    ui->side_setting_label->ShowRedPoint(false); // 选中后取消红点

    ui->stackedWidget->setCurrentWidget(ui->user_info_page);
}

// 聊天会话加载完成
void ChatDialog::slot_tcp_load_chat_finish(QJsonArray jsonArray)
{
    // 添加聊天列表数据
    for (const auto & chat : jsonArray) {
        auto obj = chat.toObject();

        auto chat_id = obj["chat_id"].toInt();

        auto chat_info = UserMgr::GetInstance()->GetChatInfo(chat_id);
        if (chat_info == nullptr) {
            continue;
        }

        // 创建自定义的ChatUserWidget
        auto * chat_user_item = new ChatUserItem();
        chat_user_item->SetChatInfo(chat_info);
        chat_user_item->SetItemType(ListItemType::CHAT_USER_ITEM);

        // 创建一个能够往QListWidget内部填充的Item
        QListWidgetItem * item = new QListWidgetItem();
        // 将item的大小设置为自定义的widget的大小
        item->setSizeHint(chat_user_item->sizeHint());
        // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
        ui->chat_user_list->addItem(item);
        ui->chat_user_list->setItemWidget(item, chat_user_item);

        _chat_item_map.insert(chat_id, item);
    }

    // 如果当前ui哪一个都没有选中，则选中第一个
    if (ui->chat_user_list->selectedItems().isEmpty()) {
        SetSelectChatItem(0);
        SetSelectChatPage(0);
    }
}

// 选中当前正在聊天的item
void ChatDialog::SetSelectChatItem(int uid) {
    // 如果没有item则返回
    if (ui->chat_user_list->count() <= 0) {
        return ;
    }

    // 如果uid小于等于0，则表示非法，默认选中第一个
    if (uid <= 0) {
        SPDLOG_WARN(
            "invalid chat uid={}, selecting first row",
            uid);
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

        // 如果当前列表没有加载完，则先加载完
        auto chat_info = chatListItem->GetChatInfo();
        _cur_chat_id = chat_info->GetChatId();

        return ;
    }

    auto chat_id = UserMgr::GetInstance()->GetUidToChatId(uid);
    auto iter_find = _chat_item_map.find(chat_id);

    if (iter_find == _chat_item_map.end()) {
        return;
    }

    ui->chat_user_list->setCurrentItem(iter_find.value());
    _cur_chat_id = chat_id;

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

    // uid非法，则选中第0行
    if (uid <= 0) {
        QListWidgetItem * _item = nullptr;
        _item = ui->chat_user_list->item(0);

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
        return;
    }

    auto chat_id = UserMgr::GetInstance()->GetUidToChatId(uid);
    auto iter_find = _chat_item_map.find(chat_id);

    if (iter_find == _chat_item_map.end()) {
        return;
    }

    // 取到item内部放入的自定义tiem
    QWidget * widget = ui->chat_user_list->itemWidget(iter_find.value());
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

// TCP请求加载更多聊天记录
void ChatDialog::TcpLoadingMoreChatMsg(int chatId, qint64 beforeMessageId) {
    QJsonObject obj;
    obj["chat_id"] = chatId;
    obj["current_msg_id"] = beforeMessageId;

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    emit TcpMgr::GetInstance()->sig_send_data(ID_LOAD_CHAT_MESSAGE_REQ, data);
}

// TCP加载更多聊天记录完成
void ChatDialog::slot_tcp_loading_more_chat_finish(
    int chat_id, std::vector<std::shared_ptr<ChatDataBase>> chat_msgs,
    bool can_load_more, qint64 next_cursor) {
    auto chat_info = UserMgr::GetInstance()->GetChatInfo(chat_id);
    if (chat_info) {
        // 仅保留分页元数据兼容旧代码，不再把整页复制进 ChatInfo::_chat_msgs。
        chat_info->SetIsCanLoadMore(can_load_more);
        chat_info->SetLastMsgId(static_cast<int>(next_cursor));
    }
    ui->chat_page->ApplyHistoryPage(chat_id, chat_msgs, can_load_more, next_cursor);
}

void ChatDialog::slot_tcp_loading_more_chat_failed(int chat_id)
{
    ui->chat_page->HistoryLoadFailed(chat_id);
}

// 服务器确认后按 UUID 更新正式 messageId 与发送状态。
void ChatDialog::slot_text_chat_msg_rsp_finish(
    int chat_id, QVector<MessageAcknowledgement> acknowledgements)
{
    ui->chat_page->ApplyDeliveryAcknowledgements(chat_id, acknowledgements);
}

void ChatDialog::slot_text_chat_msg_failed(int chat_id, QVector<QString> client_message_ids)
{
    ui->chat_page->MarkMessagesFailed(chat_id, client_message_ids);
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
    SPDLOG_DEBUG("loading contact list");

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
}

// 别人添加我为好友，好友列表显示逻辑
void ChatDialog::slot_tcp_add_friend_apply(std::shared_ptr<ApplyInfo> applyInfo)
{
    SPDLOG_DEBUG("friend application received from TCP");

    // 先判断是否已经添加过请求
    int b_already_apply = UserMgr::GetInstance()->AlreadyApplyAddFriend(applyInfo->_apply_uid);
    if (b_already_apply) {
        SPDLOG_DEBUG("duplicate friend application ignored");
        return ;
    }

    // 插入请求添加好友列表
    UserMgr::GetInstance()->AddApply(applyInfo->_apply_uid, applyInfo);

    // 当前选中的不是联系人处后，添加红点提示
    if (ui->side_user_label->GetCurState() == ClickLabelState::Normal) {
        // 展示左侧联系人处的红点提醒
        ui->side_user_label->ShowRedPoint(true);
    }
    // 设置新的朋友item处的红点提醒
    ui->contact_user_list->ShowRedPoint(true);
    // 将新的请求插入到列表中
    ui->apply_friend_page->AddNewApply(applyInfo);
}

