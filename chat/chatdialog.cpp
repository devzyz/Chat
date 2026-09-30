#include "chatdialog.h"
#include "clientrequests.h"
#include "logmgr.h"
#include "ui_chatdialog.h"
#include <QAction>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>
#include <QRandomGenerator>
#include "chatuseritem.h"
#include "loadingdialog.h"
#include <QThread>
#include <QMouseEvent>
#include "tcpmgr.h"
#include "usermgr.h"
#include "messageservice.h"
#include "chatuseritem.h"
#include "contactuseritem.h"
#include <QTimer>
#include <QJsonDocument>
#include <QByteArray>

ChatDialog::ChatDialog(QWidget *parent)
    : QDialog(parent), _mode(ChatUIMode::ChatMode), _state(ChatUIMode::ChatMode)
    , ui(new Ui::ChatDialog), _b_chat_loading(false), _cur_chat_id(0)
{
    ui->setupUi(this);

    // 添加按钮的高亮设置
    ui->add_btn->setState("normal", "hover", "press");
    ui->add_btn->setToolTip(tr("创建群聊"));
    connect(ui->add_btn, &QPushButton::clicked, this, &ChatDialog::openCreateGroup);

    // 搜索框最大长度限制
    ui->search_edit->setMaxLength(15);
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
    QPixmap pixmap = UserMgr::instance()->selfAvatar();
    pixmap = pixmap.scaled(ui->side_head_label->size(), Qt::KeepAspectRatio);
    ui->side_head_label->setPixmap(pixmap);
    ui->side_head_label->setScaledContents(true);
    connect(UserMgr::instance()->localAvatar(), &LocalAvatar::imageChanged, this,
        /** @brief 头像变化时刷新侧栏本人头像。 */
        [this]() {
        ui->side_head_label->setPixmap(UserMgr::instance()->selfAvatar().scaled(
            ui->side_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });

    // 设置左侧菜单栏状态
    ui->side_chat_label->setState("leave", "hover", "select");
    ui->side_user_label->setState("leave", "hover", "select");
    ui->side_setting_label->setState("leave", "hover", "select");

    addLabelGroup(ui->side_chat_label);
    addLabelGroup(ui->side_user_label);
    addLabelGroup(ui->side_setting_label);

    // 设置聊天为默认选中界面
    ui->side_chat_label->setSelected(true);

    // 默认隐藏
    showSearch(false);

    // 将search_edit关联到，search_list中用于搜索逻辑的_search_edit
    ui->search_list->setSearchEdit(ui->search_edit);

    // 当需要显示搜索框内的清除图标时，更改为实际的清除图标
    connect(ui->search_edit, &QLineEdit::textChanged,
        /** @brief 按搜索框是否为空切换清除按钮图标。 */
        [clearAction](const QString& text) {
        if (!text.isEmpty()) {
            clearAction->setIcon(QIcon(":/res/close_search.png"));
        }else {
            clearAction->setIcon(QIcon(":/res/close_transport.png"));
        }
    });

    // 点击清除图标后的逻辑
    // 清空输入框，清除图标删除，失去焦点
    connect(clearAction, &QAction::triggered,
        /** @brief 清空搜索内容、焦点及搜索结果列表。 */
        [this, clearAction]() {
        ui->search_edit->clear();
        clearAction->setIcon(QIcon(":/res/close_transport.png"));
        ui->search_edit->clearFocus();

        // 隐藏搜索列表
        showSearch(false);
    });

    // 连接加载更多聊天列表的信号与槽
    connect(ui->chat_user_list, &ChatUserList::moreChatsRequested, this, &ChatDialog::loadingChatList);

    // 切换当前QListWidget为聊天记录widget
    connect(ui->side_chat_label, &StateWidget::clicked, this, &ChatDialog::midlistToChatList);

    // 切换当前QListWidget为联系人widget
    connect(ui->side_user_label, &StateWidget::clicked, this, &ChatDialog::midlistToUserList);

    // 切换右侧界面为Setting界面
    connect(ui->side_setting_label, &StateWidget::clicked, this, &ChatDialog::switchUserInfoPage);

    // 切换当前QListWidget为搜索框，当搜索列表不为空的时候
    connect(ui->search_edit, &QLineEdit::textChanged, this, &ChatDialog::searchEditTextChanged);

    // 连接触发新朋友Page的信号
    connect(ui->contact_user_list, &ContactUserList::friendApplicationsRequested,
            this, &ChatDialog::switchApplyFriendListPage);

    // 连接添加好友申请信号
    connect(TcpMgr::instance().get(), &TcpMgr::friendApplicationReceived,
            this, &ChatDialog::tcpAddFriendApply);


    // 连接搜索到好友后，跳转到与该好友的聊天界面的信号
    connect(ui->search_list, &SearchList::chatRequested, this, &ChatDialog::fromSearchJumpChatItem);

    // 在联系人列表，点击联系人后，右侧跳转到对应的联系人信息页面
    connect(ui->contact_user_list, &ContactUserList::friendDetailsRequested,
            this, &ChatDialog::switchFriendInfoPage);

    // 在用户详细信息界面，点击聊天后，跳转到聊天页面
    connect(ui->friend_info_page, &FriendInfoPage::chatRequested,
            this, &ChatDialog::fromFriendJumpChatItem);

    // 连接聊天列表点击信号
    connect(ui->chat_user_list, &QListWidget::itemClicked, this, &ChatDialog::chatItemClicked);

    // 连接发送文本信息后，将发送的文本插入到聊天记录中
    connect(ui->chat_page, &ChatPage::outgoingTextQueued,
            this, &ChatDialog::appendSendTextCacheMsg);
    connect(ui->chat_page, &ChatPage::historyRequested,
            this, &ChatDialog::tcpLoadingMoreChatMsg);
    auto *messages = UserMgr::instance()->messages();
    connect(messages, &MessageService::historyLoaded, ui->chat_page, &ChatPage::applyStoredHistory);
    connect(messages, &MessageService::sendFailed, ui->chat_page, &ChatPage::markMessagesFailed);
    connect(messages, &MessageService::historyLoaded, this,
        /** @brief 用初始本地历史更新会话列表的最新消息摘要。 */
        [this](int chatId, qint64 before, const QVector<StoredMessage> &rows, bool) {
            if (before != 0 || rows.isEmpty() || !_chat_item_map.contains(chatId)) return;
            auto *item = qobject_cast<ChatUserItem*>(ui->chat_user_list->itemWidget(_chat_item_map.value(chatId)));
            if (!item) return;
            QString summary = rows.back().content;
            if (summary.startsWith("@resource:v1:")) {
                summary = QJsonDocument::fromJson(summary.mid(13).toUtf8()).object()["name"].toString();
            }
            item->setLastTextChatMsg(summary);
        });
    connect(messages, &MessageService::messagesChanged, this,
        /** @brief 消息事实变化后重新加载当前可见范围。 */
        [this, messages](int chatId) {
        messages->loadHistory(chatId, 0, ui->chat_page->oldestLoadedMessageId(chatId));
    });
    connect(messages, &MessageService::failed, this,
        /** @brief 结束失败的历史加载并显示存储错误。 */
        [this](int chatId, const QString &reason) {
        ui->chat_page->historyLoadFailed(chatId);
        ui->chat_page->setToolTip(reason);
        SPDLOG_WARN("local message operation failed, chat_id={}, reason={}", chatId, LogMgr::toUtf8(reason));
    });

    // 连接服务器通知我添加消息后的信号，将服务器通知的信息刷新到聊天界面上
    connect(TcpMgr::instance().get(), &TcpMgr::chatMessagesReceived,
            this, &ChatDialog::updateTextChatMsg);

    connect(messages, &MessageService::directoryPageLoaded, this,
        /** @brief 展示 SQLite 会话页，界面游标与网络同步游标独立。 */
        [this](const QString &kind, int after, const QJsonArray &rows, bool more) {
            if (kind != "conversations" || after != _chatCursor) return;
            QJsonArray chats;
            for (const auto &value : rows) chats.append(QJsonObject{{"chat_id", value.toObject()["id"]}});
            tcpLoadChatFinish(chats);
            if (!rows.isEmpty()) _chatCursor = rows.last().toObject()["id"].toInt();
            _hasMoreChats = more;
            _b_chat_loading = false;
        });
    connect(messages, &MessageService::directoryChanged, this,
        /** @brief 本地提交后更新已展示行，未展示会话保持可分页读取。 */
        [this](const QJsonObject &directory) {
            const bool wasComplete = !_hasMoreChats;
            for (const auto &value : directory["conversations"].toArray()) {
                const int id = value.toObject()["id"].toInt();
                if (id <= _chatCursor) tcpLoadChatFinish(QJsonArray{QJsonObject{{"chat_id", id}}});
                else _hasMoreChats = true;
            }
            for (const auto &value : directory["contacts"].toArray()) {
                const int chat = UserMgr::instance()->privateChatIdFor(value.toObject()["id"].toInt());
                if (_chat_item_map.contains(chat)) tcpLoadChatFinish(QJsonArray{QJsonObject{{"chat_id", chat}}});
            }
            if (_chat_item_map.isEmpty() || wasComplete) loadChatUserList();
        });
    connect(messages, &MessageService::directoryFailed, this,
        /** @brief 目录查询失败后恢复加载入口。 */
        [this](const QString &kind) { if (kind.isEmpty() || kind == "conversations") _b_chat_loading = false; });
    loadChatUserList();

    // 连接创建私聊请求完成函数
    connect(TcpMgr::instance().get(), &TcpMgr::privateChatCreated,
            this, &ChatDialog::createPrivateChatFinish);

    // 连接增量加载聊天记录完成
    connect(TcpMgr::instance().get(), &TcpMgr::chatHistoryLoaded,
            this, &ChatDialog::tcpLoadingMoreChatFinish);
    connect(TcpMgr::instance().get(), &TcpMgr::chatHistoryFailed,
            this, &ChatDialog::tcpLoadingMoreChatFailed);

    // 连接服务器回包之后的状态更新
    connect(TcpMgr::instance().get(), &TcpMgr::messagesAcknowledged,
            this, &ChatDialog::textChatMsgRspFinish);
    connect(TcpMgr::instance().get(), &TcpMgr::messagesFailed,
            this, &ChatDialog::textChatMsgFailed);
}

ChatDialog::~ChatDialog()
{
    delete ui;
}

// 获取一部分聊天列表
void ChatDialog::loadChatUserList()
{
    if (_b_chat_loading || !_hasMoreChats) return;
    _b_chat_loading = true;
    UserMgr::instance()->messages()->loadDirectoryPage("conversations", _chatCursor, LOADING_STEP_LENGTH);
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
 * @brief ChatDialog::showSearch
 * @param bsearch
 * 下方有三种形式，聊天列表，搜索列表，联系人列表
 */
void ChatDialog::showSearch(bool bsearch)
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
 * @brief ChatDialog::addLabelGroup
 * @param label
 * 用一个列表存储左侧在同一个组里的StateWdiget组件
 * 因为右边的显示区域只会显示一个
 */
void ChatDialog::addLabelGroup(StateWidget *label)
{
    _label_list.push_back(label);
}

// 清除其他stateWidget的选中状态，为了保持只有一个被选中
void ChatDialog::clearLabelState(StateWidget *label)
{
    for (auto & ele : _label_list) {
        if (ele == label) {
            continue;
        }
        ele->clearState();
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
        showSearch(false);
    }
}

/**
 * @brief ChatDialog::slot_loading_chat_user
 * 加載更多用戶聊天列表
 */
void ChatDialog::loadingChatList()
{
    loadChatUserList();
}

/**
 * @brief ChatDialog::slot_side_chat
 * 点击左侧聊天后，列表切换到聊天记录列表
 */
void ChatDialog::midlistToChatList()
{
    SPDLOG_DEBUG("chat navigation selected");
    // 传入聊天StateWidget
    clearLabelState(ui->side_chat_label);
    ui->side_chat_label->showRedPoint(false); // 选中后取消红点
    // 设置右面为聊天界面
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    _state = ChatUIMode::ChatMode;
    showSearch(false);
}

/**
 * @brief ChatDialog::slot_side_user
 * 点击左侧联系人后，列表切换到联系人列表
 */
void ChatDialog::midlistToUserList()
{
    SPDLOG_DEBUG("contacts navigation selected");
    clearLabelState(ui->side_user_label);
    ui->side_user_label->showRedPoint(false); // 选中后取消红点
    // 设置右面为好友申请列表
    ui->stackedWidget->setCurrentWidget(ui->apply_friend_page);
    _state = ChatUIMode::ContactMode;
    showSearch(false);
}

// 如果搜索框不空，则显示搜索列表
void ChatDialog::searchEditTextChanged(const QString &str)
{
    if (!str.isEmpty()) {
        showSearch(true);
    }
}

/**
 * @brief ChatDialog::slot_switch_apply_friend_page
 * 切换到好友申请列表
 */
void ChatDialog::switchApplyFriendListPage()
{
    SPDLOG_DEBUG("switching to friend application page");
    ui->stackedWidget->setCurrentWidget(ui->apply_friend_page);
}

/**
 * @brief ChatDialog::addNewChat
 * @param chat_info
 * 添加新的会话到聊天列表中
 */
void ChatDialog::addNewChat(std::shared_ptr<ChatInfo> chat_info) {
    // 创建自定义的ChatUserWidget
    auto * chat_user_item = new ChatUserItem();
    chat_user_item->setChatInfo(chat_info);

    // 创建一个能够往QListWidget内部填充的Item
    QListWidgetItem * item = new QListWidgetItem();
    // 将item的大小设置为自定义的widget的大小
    item->setSizeHint(chat_user_item->sizeHint());
    // 将这个item插入到最上面
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_item);
    // key 为chat_id
    _chat_item_map.insert(chat_info->getChatId(), item);
}

/**
 * @brief 收到会话通知后补充聊天列表。
 * @param chat_info 已认证会话的展示信息。
 */
void ChatDialog::tcpAddChatList(std::shared_ptr<ChatInfo> chat_info)
{
    SPDLOG_DEBUG("authenticated friend received from TCP");
    addNewChat(chat_info);
}

// 搜索到的人是好友后，跳转到与该好友的聊天界面
/** @brief 从搜索结果打开已有聊天，缺失会话时发起私聊创建。 */
void ChatDialog::fromSearchJumpChatItem(std::shared_ptr<SearchInfo> si)
{
    SPDLOG_DEBUG("opening chat from search result");
    auto chat_id = UserMgr::instance()->privateChatIdFor(si->_uid);

    if (chat_id == -1) {
        QJsonObject json;
        json["other_uid"] = si->_uid;
        json["other_name"] = si->_name;
        json["other_description"] = si->_description;
        json["other_icon"] = si->_icon;
        json["other_sex"] = si->_sex;
        loadOncePrivateChat(UserMgr::instance()->uid(), si->_uid, json);
        return;
    }

    chat_id = UserMgr::instance()->privateChatIdFor(si->_uid);

    // 取出他对应的item
    auto find_iter = _chat_item_map.find(chat_id);
    if (find_iter == _chat_item_map.end()) {
        const auto chat = UserMgr::instance()->chatInfo(chat_id);
        if (!chat) return;
        addNewChat(chat);
        find_iter = _chat_item_map.find(chat_id);
    }
    ui->chat_user_list->scrollToItem(find_iter.value()); // 将列表滚动到用户可以见的viewport区域
    ui->side_chat_label->setSelected(true); // 选中侧边栏中的聊天
    ui->side_chat_label->showRedPoint(false); // 选中后，红点消失
    // 设置item为选中状态
    setSelectChatItem(si->_uid);
    // 更新右侧对应的详细聊天记录
    setSelectChatPage(si->_uid);
    // 切换真正的list页面
    midlistToChatList();
}

/** @brief 插入新私聊列表项并选中对应聊天页。 */
void ChatDialog::createPrivateChatFinish(std::shared_ptr<ChatInfo> chat_info) {
    if (!_chat_item_map.contains(chat_info->getChatId())) addNewChat(chat_info);
    auto *item = _chat_item_map.value(chat_info->getChatId());
    ui->chat_user_list->scrollToItem(item); // 将列表滚动到用户可以见的viewport区域
    ui->side_chat_label->setSelected(true); // 选中侧边栏中的聊天
    ui->side_chat_label->showRedPoint(false); // 选中后，红点消失
    // 设置item为选中状态
    setSelectChatItem(chat_info->getUid());
    // 更新右侧对应的详细聊天记录
    setSelectChatPage(chat_info->getUid());
    // 切换真正的list页面
    midlistToChatList();
}

// 在好友详细信息界面，点击聊天后跳转到聊天界面
/** @brief 从好友详情打开聊天，缺失会话时请求创建。 */
void ChatDialog::fromFriendJumpChatItem(std::shared_ptr<UserInfo> si)
{
    SPDLOG_DEBUG("opening chat from user information");
    auto chat_id = UserMgr::instance()->privateChatIdFor(si->_uid);

    if (chat_id == -1) {
        QJsonObject json;
        json["other_uid"] = si->_uid;
        json["other_name"] = si->_name;
        json["other_description"] = si->_description;
        json["other_icon"] = si->_icon;
        json["other_sex"] = si->_sex;
        loadOncePrivateChat(UserMgr::instance()->uid(), si->_uid, json);
        return;
    }

    chat_id = UserMgr::instance()->privateChatIdFor(si->_uid);

    // 取出他对应的item
    auto find_iter = _chat_item_map.find(chat_id);
    if (find_iter == _chat_item_map.end()) {
        const auto chat = UserMgr::instance()->chatInfo(chat_id);
        if (!chat) return;
        addNewChat(chat);
        find_iter = _chat_item_map.find(chat_id);
    }
    ui->chat_user_list->scrollToItem(find_iter.value()); // 将列表滚动到用户可以见的viewport区域
    ui->side_chat_label->setSelected(true); // 选中侧边栏中的聊天
    ui->side_chat_label->showRedPoint(false); // 选中后，红点消失
    // 设置item为选中状态
    setSelectChatItem(si->_uid);
    // 更新右侧对应的详细聊天记录
    setSelectChatPage(si->_uid);
    // 切换真正的list页面
    midlistToChatList();
}

void ChatDialog::loadOncePrivateChat(int self_id, int other_id, QJsonObject json)
{
    emit TcpMgr::instance()->sendRequested(ID_CREATE_PRIVATE_CHAT_REQ,
        clientPrivateChatRequest(self_id, other_id, json));
}


// 右侧跳转到好友详细信息界面
/** @brief 将右侧页面切换为给定好友的详情。 */
void ChatDialog::switchFriendInfoPage(std::shared_ptr<UserInfo> friend_info)
{
    SPDLOG_DEBUG("switching to friend information page");
    ui->stackedWidget->setCurrentWidget(ui->friend_info_page);
    ui->friend_info_page->setInfo(friend_info);
}

// 当聊天列表的item被点击后，触发的槽函数
/** @brief 处理聊天列表选择并重置该项提示，更新当前会话。 */
void ChatDialog::chatItemClicked(QListWidgetItem * item)
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
    auto itemType = itembase->getItemType();
    if (itemType == ListItemType::INVALID_ITEM ||
        itemType == ListItemType::GROUP_TIP_ITEM) {
        SPDLOG_WARN("invalid chat list item clicked");
        return ;
    }

    // 如果是聊天类型，则进行转换
    if (itemType == ListItemType::CHAT_USER_ITEM) {
        SPDLOG_DEBUG("chat user item clicked");

        auto chat_item = qobject_cast<ChatUserItem*> (itembase);
        auto chat_info = chat_item->getChatInfo();
        chat_item->resetNewMsgCount(); // 被点击后，重置红点的刷新

        _cur_chat_id = chat_info->getChatId();
        // 设置右侧的聊天界面
        ui->chat_page->setChatInfo(chat_info);
        _cur_chat_id = chat_info->getChatId();
    }
}

// 将发送的文本插入到聊天缓存中
/** @brief 把待发文本按 UUID 加入对应会话缓存；找不到会话项时返回。 */
void ChatDialog::appendSendTextCacheMsg(QString uuid, std::shared_ptr<ChatDataBase> text_chat_data)
{
    SPDLOG_DEBUG("appending outgoing text chat message");
    int chat_id = text_chat_data->getChatId();
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
    auto itemType = baseItem->getItemType();
    if (itemType == ListItemType::CHAT_USER_ITEM) {
        auto * chat_item = qobject_cast<ChatUserItem*> (baseItem);
        if (!chat_item) {
            return ;
        }

        // 将发送的信息放入聊天记录中
        auto chat_info = chat_item->getChatInfo();
        chat_info->addCacheChatData(uuid, text_chat_data);

        return ;
    }
}

// 将服务器通知的信息，刷新到界面上
/** @brief 把消息写入所属会话模型并更新非当前会话的新消息提示。 */
void ChatDialog::updateTextChatMsg(int from_uid, int to_uid, int chat_id, std::vector<std::shared_ptr<ChatDataBase>>& msgs)
{
    Q_UNUSED(from_uid);
    Q_UNUSED(to_uid);
    // 始终写入 chatId 对应的常驻 Model；非当前会话不会操作当前 View。
    for (const auto &msg : msgs) {
        ui->chat_page->appendChatMsg(msg);
    }

    // 如果不在聊天界面，则将聊天界面红点显示出来
    auto _side_chat_label_isSelect = ui->side_chat_label->getCurState();
    if (_side_chat_label_isSelect == ClickLabelState::Normal) {
        ui->side_chat_label->showRedPoint(true);
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
        auto itemType = baseItem->getItemType();
        if (itemType == ListItemType::CHAT_USER_ITEM) {
            auto * chat_item = qobject_cast<ChatUserItem*> (baseItem);
            if (!chat_item) {
                return ;
            }

            // 更新红点
            chat_item->updateNewMsgCount(msgs.size());
            return ;
        }
        return ;
    }
}

void ChatDialog::switchUserInfoPage()
{
    SPDLOG_DEBUG("settings navigation selected");
    SPDLOG_DEBUG("switching to settings page");
    // 传入聊天StateWidget
    clearLabelState(ui->side_setting_label);
    ui->side_setting_label->showRedPoint(false); // 选中后取消红点

    ui->stackedWidget->setCurrentWidget(ui->user_info_page);
}

// 聊天会话加载完成
/** @brief 按服务端会话列表建立缺失展示项并请求本地历史。 */
void ChatDialog::tcpLoadChatFinish(QJsonArray jsonArray)
{
    // 添加聊天列表数据
    for (const auto & chat : jsonArray) {
        auto obj = chat.toObject();

        auto chat_id = obj["chat_id"].toInt();
        if (_chat_item_map.contains(chat_id)) {
            auto *widget = qobject_cast<ChatUserItem*>(ui->chat_user_list->itemWidget(_chat_item_map.value(chat_id)));
            if (widget) widget->setChatInfo(UserMgr::instance()->chatInfo(chat_id));
            UserMgr::instance()->messages()->loadHistory(chat_id);
            continue;
        }

        auto chat_info = UserMgr::instance()->chatInfo(chat_id);
        if (chat_info == nullptr) {
            continue;
        }

        // 创建自定义的ChatUserWidget
        auto * chat_user_item = new ChatUserItem();
        chat_user_item->setChatInfo(chat_info);
        chat_user_item->setItemType(ListItemType::CHAT_USER_ITEM);

        // 创建一个能够往QListWidget内部填充的Item
        QListWidgetItem * item = new QListWidgetItem();
        // 将item的大小设置为自定义的widget的大小
        item->setSizeHint(chat_user_item->sizeHint());
        // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
        ui->chat_user_list->addItem(item);
        ui->chat_user_list->setItemWidget(item, chat_user_item);

        _chat_item_map.insert(chat_id, item);
        UserMgr::instance()->messages()->loadHistory(chat_id);
    }

    // 如果当前ui哪一个都没有选中，则选中第一个
    if (ui->chat_user_list->selectedItems().isEmpty()) {
        setSelectChatItem(0);
        setSelectChatPage(0);
    }
}

// 选中当前正在聊天的item
void ChatDialog::setSelectChatItem(int uid) {
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
        chatListItem->resetNewMsgCount(); // 选中后，将新消息提醒关闭

        // 如果当前列表没有加载完，则先加载完
        auto chat_info = chatListItem->getChatInfo();
        _cur_chat_id = chat_info->getChatId();

        return ;
    }

    auto chat_id = UserMgr::instance()->privateChatIdFor(uid);
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

    chatListItem->resetNewMsgCount(); // 选中后，将新消息提醒关闭
}

// 设置右侧详细聊天记录界面
void ChatDialog::setSelectChatPage(int uid) {
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
        auto chat_info = chatListItem->getChatInfo();
        ui->chat_page->setChatInfo(chat_info);
        return;
    }

    auto chat_id = UserMgr::instance()->privateChatIdFor(uid);
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
    auto chat_info = chatListItem->getChatInfo();
    ui->chat_page->setChatInfo(chat_info);
}

// TCP请求加载更多聊天记录
void ChatDialog::tcpLoadingMoreChatMsg(int chatId, qint64 beforeMessageId) {
    UserMgr::instance()->messages()->loadHistory(chatId, beforeMessageId);
}

// TCP加载更多聊天记录完成
/** @brief 应用会话的历史消息页，并更新分页游标及后续页标志。 */
void ChatDialog::tcpLoadingMoreChatFinish(
    int chat_id, std::vector<std::shared_ptr<ChatDataBase>> chat_msgs,
    bool can_load_more, qint64 next_cursor) {
    auto chat_info = UserMgr::instance()->chatInfo(chat_id);
    if (chat_info) {
        // 仅保留分页元数据兼容旧代码，不再把整页复制进 ChatInfo::_chat_msgs。
        chat_info->setCanLoadMore(can_load_more);
        chat_info->setLastMsgId(static_cast<int>(next_cursor));
    }
    ui->chat_page->applyHistoryPage(chat_id, chat_msgs, can_load_more, next_cursor);
}

/** @brief 结束指定会话的历史加载状态，允许后续重试。 */
void ChatDialog::tcpLoadingMoreChatFailed(int chat_id)
{
    ui->chat_page->historyLoadFailed(chat_id);
}

// 服务器确认后按 UUID 更新正式 messageId 与发送状态。
/** @brief 处理发送确认，更新缓存消息与展示状态。 */
void ChatDialog::textChatMsgRspFinish(
    int chat_id, QVector<MessageAcknowledgement> acknowledgements)
{
    ui->chat_page->applyDeliveryAcknowledgements(chat_id, acknowledgements);
}

/** @brief 处理文本发送失败并更新对应消息状态。 */
void ChatDialog::textChatMsgFailed(int chat_id, QVector<QString> client_message_ids)
{
    ui->chat_page->markMessagesFailed(chat_id, client_message_ids);
}

// 别人添加我为好友，好友列表显示逻辑
/** @brief 将新好友申请加入页面并更新导航提示。 */
void ChatDialog::tcpAddFriendApply(std::shared_ptr<ApplyInfo> applyInfo)
{
    SPDLOG_DEBUG("friend application received from TCP");

    // 当前选中的不是联系人处后，添加红点提示
    if (ui->side_user_label->getCurState() == ClickLabelState::Normal) {
        // 展示左侧联系人处的红点提醒
        ui->side_user_label->showRedPoint(true);
    }
    // 设置新的朋友item处的红点提醒
    ui->contact_user_list->showRedPoint(true);
    // 申请行已由本地目录提交通知刷新。
    Q_UNUSED(applyInfo);
}

void ChatDialog::openCreateGroup()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("创建群聊"));
    dialog->resize(360, 440);
    auto *layout = new QVBoxLayout(dialog);
    auto *name = new QLineEdit(dialog);
    name->setPlaceholderText(tr("群名称（最多 20 个字）"));
    name->setMaxLength(20);
    layout->addWidget(name);
    layout->addWidget(new QLabel(tr("选择 1～19 位好友，建群后成员固定"), dialog));
    auto *members = new QListWidget(dialog);
    for (const auto &user : UserMgr::instance()->friends()) {
        auto *item = new QListWidgetItem(user->_name + QString(" (%1)").arg(user->_uid), members);
        item->setData(Qt::UserRole, user->_uid);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(members);
    auto *status = new QLabel(dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("创建"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    auto request = std::make_shared<QJsonObject>();
    auto *timeout = new QTimer(dialog);
    timeout->setSingleShot(true);
    timeout->setInterval(10000);
    connect(timeout, &QTimer::timeout, dialog,
        /** @brief 超时保留原始建群身份和内容，允许安全重试。 */
        [buttons, status] {
            status->setText(tr("尚未确认结果，请重试；也可关闭后等待群列表更新。"));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        });
    connect(buttons, &QDialogButtonBox::accepted, dialog,
        /** @brief 验证选择后发送一次固定身份的建群请求。 */
        [this, name, members, status, buttons, timeout, request] {
            if (request->isEmpty()) {
                QJsonArray ids;
                for (int i = 0; i < members->count(); ++i) {
                    if (members->item(i)->checkState() == Qt::Checked) ids.append(members->item(i)->data(Qt::UserRole).toInt());
                }
                const auto title = name->text().trimmed();
                if (title.isEmpty() || title.toUtf8().size() > 60 || ids.isEmpty() || ids.size() > 19) {
                    status->setText(tr("请填写群名，并选择 1～19 位好友。"));
                    return;
                }
                *request = {{"name", title}, {"members", ids},
                    {"request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
                name->setEnabled(false); members->setEnabled(false);
            }
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            status->setText(tr("正在创建…"));
            timeout->start();
            emit TcpMgr::instance()->sendRequested(ID_CREATE_GROUP_REQ, QJsonDocument(*request).toJson(QJsonDocument::Compact));
        });
    connect(TcpMgr::instance().get(), &TcpMgr::groupCreated, dialog,
        /** @brief 对应群落盘后选中会话；失败保留原始内容供幂等重试。 */
        [this, dialog, request, timeout, buttons, status](const QJsonObject &result) {
            if (request->isEmpty() || result["request_id"] != (*request)["request_id"]) return;
            timeout->stop();
            if (result["error"].toInt(-1) != 0) {
                status->setText(tr("创建失败，可重试。若需修改成员，请关闭后重新创建。"));
                buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
                // Keep UUID even for uncertain storage errors; editing requires reopening the form.
                return;
            }
            const int chat = result["chat_id"].toInt();
            tcpLoadChatFinish(QJsonArray{QJsonObject{{"chat_id", chat}}});
            if (_chat_item_map.contains(chat)) {
                midlistToChatList();
                ui->chat_user_list->setCurrentItem(_chat_item_map.value(chat));
                chatItemClicked(_chat_item_map.value(chat));
            }
            dialog->accept();
        });
    dialog->open();
}
