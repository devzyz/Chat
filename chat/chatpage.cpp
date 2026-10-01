#include "chatpage.h"
#include "messagesubmissioncontroller.h"
#include "grouppanel.h"
#include "clientmessage.h"
#include "clientrequests.h"
#include "messageservice.h"
#include "messagereadtracker.h"
#include "resourcetransfermanager.h"

#include "global.h"
#include "logmgr.h"
#include "messageitemdelegate.h"
#include "tcpmgr.h"
#include "ui_chatpage.h"
#include "usermgr.h"

#include <QJsonDocument>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QStyleOption>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>

ChatPage::ChatPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChatPage)
{
    ui->setupUi(this);
    auto *details = new QPushButton(tr("资料 / 备注"), this);
    auto *search = new QPushButton(tr("查找历史"), this);
    ui->horizontalLayout_3->addWidget(details); ui->horizontalLayout_3->addWidget(search);
    connect(search, &QPushButton::clicked, this, &ChatPage::openHistorySearch);
    connect(details, &QPushButton::clicked, this, /** @brief 群打开成员资料，私聊修改当前好友个人备注。 */ [this] {
        if (!_chatInfo) return;
        if (_chatInfo->getChatType() == ChatType::GROUP) {
            auto *panel = findChild<QDialog*>(QString("group-details-%1").arg(_currentChatId));
            if (!panel) panel = new GroupPanel(_currentChatId,this);
            panel->show(); panel->raise(); return;
        }
        bool ok = false;
        const auto name = QInputDialog::getText(this,tr("好友备注"),tr("仅自己可见，可留空"),QLineEdit::Normal,QString(),&ok);
        if (!ok || name.toUtf8().size() > 60) return;
        _remarkRequest = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject request{{"target_uid",_chatInfo->getUid()},{"name",name},{"request_id",_remarkRequest}};
        emit TcpMgr::instance()->sendRequested(ID_FRIEND_REMARK_REQ,QJsonDocument(request).toJson(QJsonDocument::Compact));
        const auto identity = _remarkRequest;
        QTimer::singleShot(10000,this, /** @brief 无回包时不宣称保存失败，允许重新设置相同备注。 */ [this,identity] {
            if (_remarkRequest != identity) return;
            _remarkRequest.clear();
            QMessageBox::information(this,tr("好友备注"),tr("结果尚未确认，请刷新联系人或重新设置备注。"));
        });
    });
    connect(TcpMgr::instance().get(), &TcpMgr::groupResponse, this, /** @brief 备注写入完成后显示结果，拒绝时显示错误。 */
        [this](ReqId id, const QJsonObject &response) {
        if (id != ID_FRIEND_REMARK_RSP || _remarkRequest.isEmpty() || response["request_id"] != _remarkRequest) return;
        _remarkRequest.clear();
        QMessageBox::information(this,tr("好友备注"),response["local_save_failed"].toBool()
            ? tr("操作已完成，本地保存失败。请刷新联系人恢复。")
            : response["error"].toInt(-1)==0 ? tr("备注已保存") : tr("备注未保存，请稍后重试"));
    });
    connect(UserMgr::instance()->messages(), &MessageService::directoryChanged, this,
        /** @brief 已落盘的群权限变化即时关闭输入。 */ [this](const QJsonObject &) { refreshGroupState(); });
    connect(UserMgr::instance().get(), &UserMgr::avatarChanged, this,
        /** @brief 更新对应发送者的消息头像。 */
        [this](int uid) {
        _messageStore.updateSenderAvatar(uid, UserMgr::instance()->avatarFor(uid));
    });
    connect(UserMgr::instance()->localAvatar(), &LocalAvatar::imageChanged, this,
        /** @brief 本人头像变化时更新所有关联消息行。 */
        [this]() {
        const auto user = UserMgr::instance();
        _messageStore.updateSenderAvatar(user->uid(), user->selfAvatar());
    });

    ui->receive_btn->setState("normal", "hover", "press");
    ui->send_btn->setState("normal", "hover", "press");
    ui->emoij_label->setState("normal", "hover", "press", "normal", "hover", "press");
    ui->file_label->setState("normal", "hover", "press", "normal", "hover", "press");

    _messageDelegate = new MessageItemDelegate(ui->chat_detail_data_list);
    ui->chat_detail_data_list->setItemDelegate(_messageDelegate);
    _readTracker = new MessageReadTracker(ui->chat_detail_data_list);
    connect(_readTracker, &MessageReadTracker::observed, UserMgr::instance()->messages(), &MessageService::observeRead);
    initResourceTransfers();
    ui->chat_edit->setAccountRoot(UserMgr::instance()->storageRoot());
    connect(ui->chat_edit, &MessageTextEdit::send, this, &ChatPage::on_send_btn_clicked);
    connect(ui->chat_edit, &MessageTextEdit::inputRejected, this,
        /** @brief 保留草稿并显示输入失败原因。 */ [this](const QString &reason) {
        QMessageBox::information(this, tr("输入未接收"), reason);
    });
    auto *submission = UserMgr::instance()->submissions();
    auto *status = new QLabel(this);
    status->setWordWrap(true);
    auto *retry = new QPushButton(tr("重试提交"), this);
    auto *cancel = new QPushButton(tr("取消未提交内容"), this);
    ui->horizontalLayout_3->addWidget(status);
    ui->horizontalLayout_3->addWidget(retry);
    ui->horizontalLayout_3->addWidget(cancel);
    const auto refresh = /** @brief 页面重建时恢复账号任务进度及操作入口。 */ [submission, status, retry, cancel] {
        status->setText(submission->status());
        status->setToolTip(submission->pendingSummary());
        retry->setVisible(submission->isFailed());
        cancel->setVisible(submission->hasPending());
    };
    connect(submission, &MessageSubmissionController::stateChanged, this, refresh);
    connect(retry, &QPushButton::clicked, submission, &MessageSubmissionController::retry);
    connect(cancel, &QPushButton::clicked, submission, &MessageSubmissionController::cancel);
    connect(submission, &MessageSubmissionController::rejected, this,
        /** @brief 提交拒绝保持编辑器原内容并说明原因。 */ [this](const QString &reason) {
        QMessageBox::information(this, tr("未提交"), reason);
    });
    refresh();
    connect(ui->chat_detail_data_list, &ChatDetailList::viewportResized, this,
        /** @brief 视口变化时清空尺寸缓存并重新布局。 */
        [this]() {
        _messageDelegate->clearSizeCache();
        ui->chat_detail_data_list->doItemsLayout();
        ui->chat_detail_data_list->viewport()->update();
    });
    connect(ui->chat_detail_data_list, &ChatDetailList::nearTopReached,
            this, &ChatPage::requestOlderHistory);
}

ChatPage::~ChatPage()
{
    _transfer->cancel();
    // QListView does not own the model. Detach it before MessageModelStore is destroyed.
    ui->chat_detail_data_list->setModel(nullptr);
    delete ui;
    ui = nullptr;
}

void ChatPage::setChatInfo(std::shared_ptr<ChatInfo> chatInfo)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!chatInfo) {
        return;
    }

    saveCurrentScrollAnchor();
    if (_currentChatId != chatInfo->getChatId()) {
        _readTracker->resetExposure();
        _remarkRequest.clear();
        _searchJumpId = 0;
    }
    _chatInfo = std::move(chatInfo);
    _currentChatId = _chatInfo->getChatId();

    ui->title_label->setText(_chatInfo->name());
    refreshGroupState();
    if (_chatInfo->getChatType() == ChatType::PRIVATE) {
        const auto friendInfo = UserMgr::instance()->friendById(_chatInfo->getUid());
        if (friendInfo) {
            ui->title_label->setText(friendInfo->_backname.isEmpty() ? friendInfo->_name : friendInfo->_backname);
        }
    }

    auto *model = _messageStore.getOrCreate(_currentChatId);
    seedModelFromLegacyData(model, _chatInfo);
    _suppressHistoryRequests = true;
    ui->chat_detail_data_list->setModel(model);
    _messageDelegate->clearSizeCache();

    const ScrollAnchor anchor = _scrollAnchors.value(_currentChatId);
    if (anchor.valid) {
        restoreScrollAnchor(_currentChatId, anchor);
    } else {
        queueScrollToBottom(_currentChatId);
    }
    const int selectedChatId = _currentChatId;
    QTimer::singleShot(0, this,
        /** @brief 仅为仍被选中的会话恢复历史加载。 */
        [this, selectedChatId]() {
        if (_currentChatId == selectedChatId) {
            _suppressHistoryRequests = false;
        }
    });

    if (!model->hasLoadedInitialPage() && !model->isLoadingHistory()) {
        requestHistory(model);
    }
    emit conversationOpened(_currentChatId);
}

qint64 ChatPage::oldestLoadedMessageId(int chatId) const
{
    const auto *model = _messageStore.find(chatId);
    return model && model->hasLoadedInitialPage() ? model->oldestMessageId() : 0;
}

void ChatPage::applyStoredHistory(int chatId, qint64 before,
                                 const QVector<StoredMessage> &messages, bool hasMore)
{
    auto *model = _messageStore.find(chatId);
    if (!model) return;
    const bool current = _currentChatId == chatId;
    const auto anchor = current ? captureScrollAnchor() : ScrollAnchor{};
    const bool initial = !model->hasLoadedInitialPage();
    QVector<MessageRecord> records;
    for (const auto &stored : messages) {
        MessageRecord record;
        record.durable = stored.messageId > 0;
        record.readConfirmed = stored.receipt == ReceiptLevel::Read;
        record.messageId = stored.messageId;
        record.chatId = chatId;
        record.senderId = stored.senderId;
        record.isSelf = stored.senderId == UserMgr::instance()->uid();
        // Client UUIDs are sender-scoped; remote rows use their server ID in the GUI index.
        if (record.isSelf) record.clientMessageId = stored.clientMessageId;
        record.sentAt = QDateTime::fromMSecsSinceEpoch(stored.sentAt);
        record.text = stored.content;
        const auto sender = record.isSelf ? UserMgr::instance()->userInfo()
                                         : UserMgr::instance()->friendById(stored.senderId);
        if (sender) { record.senderName = sender->_name; record.avatarKey = sender->_icon; }
        else record.senderName = tr("用户 %1").arg(stored.senderId);
        record.avatar = UserMgr::instance()->avatarFor(stored.senderId, record.avatarKey);
        record.deliveryStatus = !record.isSelf ? DeliveryStatus::None :
            stored.receipt == ReceiptLevel::Read ? DeliveryStatus::Read :
            stored.receipt == ReceiptLevel::Delivered ? DeliveryStatus::Delivered :
            stored.state == StoredMessage::Confirmed ? DeliveryStatus::Sent :
            stored.state == StoredMessage::Failed ? DeliveryStatus::Failed :
            stored.state == StoredMessage::Queued ? DeliveryStatus::Queued :
            stored.state == StoredMessage::Pending ? DeliveryStatus::Sending : DeliveryStatus::Uncertain;
        loadResource(record);
        records.push_back(record);
    }
    model->mergeMessages(records);
    if (before > 0 || initial || model->oldestMessageId() == 0) model->setCanLoadMore(hasMore);
    else if (hasMore) model->setCanLoadMore(true);
    model->setHistoryCursor(model->oldestMessageId());
    model->setInitialPageLoaded(true);
    model->setLoadingHistory(false);
    if (current) {
        _messageDelegate->clearSizeCache();
        // A periodic refresh must not consume the pending search-range navigation.
        if (_searchJumpId > 0 && before == _searchJumpId + 1) {
            const auto target = _searchJumpId; _searchJumpId = 0;
            QTimer::singleShot(0,this,/** @brief 模型完成布局后定位搜索命中的消息。 */ [this,chatId,target] {
                if (_currentChatId != chatId) return;
                auto *model = ui->chat_detail_data_list->model();
                for (int row=0;row<model->rowCount();++row) {
                    const auto index=model->index(row,0);
                    if (index.data(MessageListModel::MessageIdRole).toLongLong()==target) {
                        ui->chat_detail_data_list->setCurrentIndex(index);
                        ui->chat_detail_data_list->scrollTo(index,QAbstractItemView::PositionAtCenter); break;
                    }
                }
            });
        } else if (initial || anchor.wasAtBottom) queueScrollToBottom(chatId);
        else restoreScrollAnchor(chatId, anchor);
    }
}

void ChatPage::appendChatMsg(const std::shared_ptr<ChatDataBase> &message)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!message) {
        return;
    }

    MessageRecord record = toMessageRecord(message);
    auto *model = _messageStore.getOrCreate(record.chatId);
    const bool isCurrent = record.chatId == _currentChatId
        && ui->chat_detail_data_list->model() == model;
    const bool shouldFollow = isCurrent
        && (record.isSelf || ui->chat_detail_data_list->isNearBottom());

    if (model->appendMessage(record) > 0 && shouldFollow) {
        queueScrollToBottom(record.chatId);
    }
}

void ChatPage::applyHistoryPage(int chatId,
                                const std::vector<std::shared_ptr<ChatDataBase>> &messages,
                                bool canLoadMore, qint64 nextCursor)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto *model = _messageStore.getOrCreate(chatId);
    const bool initialPage = !model->hasLoadedInitialPage();
    const bool affectsCurrentView = chatId == _currentChatId
        && ui->chat_detail_data_list->model() == model;
    if (affectsCurrentView) {
        _suppressHistoryRequests = true;
    }
    const ScrollAnchor anchor = (!initialPage && affectsCurrentView)
        ? captureScrollAnchor() : ScrollAnchor{};

    QVector<MessageRecord> records;
    records.reserve(static_cast<qsizetype>(messages.size()));
    for (const auto &message : messages) {
        if (message) {
            records.push_back(toMessageRecord(message));
        }
    }

    if (!_messageStore.applyHistory(chatId, records, canLoadMore, nextCursor)) {
        if (affectsCurrentView) {
            QTimer::singleShot(0, this,
                /** @brief 本地历史完成后解除当前会话的请求抑制。 */
                [this, chatId] {
                if (_currentChatId == chatId) _suppressHistoryRequests = false;
            });
        }
        return;
    }

    if (!affectsCurrentView) {
        return;
    }
    if (initialPage) {
        queueScrollToBottom(chatId);
    } else if (anchor.valid) {
        restoreScrollAnchor(chatId, anchor);
    }
    QTimer::singleShot(0, this,
        /** @brief 仅解除仍匹配当前会话的历史请求抑制。 */
        [this, chatId]() {
        if (_currentChatId == chatId) {
            _suppressHistoryRequests = false;
        }
    });
}

void ChatPage::historyLoadFailed(int chatId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (auto *model = _messageStore.find(chatId)) {
        model->setLoadingHistory(false);
    }
}

void ChatPage::applyDeliveryAcknowledgements(
    int chatId, const QVector<MessageAcknowledgement> &acknowledgements)
{
    Q_ASSERT(QThread::currentThread() == thread());
    _messageStore.acknowledge(chatId, acknowledgements, UserMgr::instance()->uid());
}

void ChatPage::markMessagesFailed(int chatId, const QVector<QString> &clientMessageIds)
{
    Q_ASSERT(QThread::currentThread() == thread());
    _messageStore.markFailed(chatId, clientMessageIds, UserMgr::instance()->uid());
}

void ChatPage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void ChatPage::on_send_btn_clicked()
{
    if (!_chatInfo) return;
    const auto draft = ui->chat_edit->draft();
    if (UserMgr::instance()->submissions()->submit(draft, _currentChatId,
            _chatInfo->getUid(), _chatInfo->getChatType() == ChatType::GROUP))
        ui->chat_edit->clearAccepted(draft.id);
}

void ChatPage::requestOlderHistory()
{
    auto *model = _messageStore.find(_currentChatId);
    if (_suppressHistoryRequests || !model || !model->hasLoadedInitialPage() || !model->canLoadMore()
        || model->isLoadingHistory()) {
        return;
    }
    requestHistory(model);
}

MessageRecord ChatPage::toMessageRecord(const std::shared_ptr<ChatDataBase> &message)
{
    auto record = clientMessageRecord(message);
    loadResource(record);
    record.avatar = UserMgr::instance()->avatarFor(record.senderId, record.avatarKey);
    return record;
}

QPixmap ChatPage::cachedAvatar(const QString &avatarKey)
{
    if (avatarKey.isEmpty()) {
        return {};
    }
    const auto found = _avatarCache.constFind(avatarKey);
    if (found != _avatarCache.cend()) {
        return found.value();
    }
    QPixmap avatar(avatarKey);
    _avatarCache.insert(avatarKey, avatar);
    return avatar;
}

void ChatPage::seedModelFromLegacyData(MessageListModel *model,
                                       const std::shared_ptr<ChatInfo> &chatInfo)
{
    if (!model || _legacySeededChats.contains(model->chatId())) {
        return;
    }
    _legacySeededChats.insert(model->chatId());

    // Temporary compatibility: pending DTOs may predate creation of this Model.
    // Confirmed/history DTOs are deliberately not copied; the Model is their source of truth.
    QVector<MessageRecord> records;
    const auto pending = chatInfo->getCacheChatMsgs();
    records.reserve(pending.size());
    for (const auto &message : pending) records.push_back(toMessageRecord(message));
    model->appendMessages(records);
}

void ChatPage::requestHistory(MessageListModel *model)
{
    if (!model || model->isLoadingHistory() || !model->canLoadMore()) {
        return;
    }
    model->setLoadingHistory(true);
    const qint64 cursor = model->hasLoadedInitialPage()
        ? (model->historyCursor() > 0 ? model->historyCursor() : model->oldestMessageId())
        : 0;
    emit historyRequested(model->chatId(), cursor);
}

ChatPage::ScrollAnchor ChatPage::captureScrollAnchor() const
{
    ScrollAnchor anchor;
    auto *model = qobject_cast<MessageListModel *>(ui->chat_detail_data_list->model());
    if (!model) {
        return anchor;
    }
    anchor.wasAtBottom = ui->chat_detail_data_list->isNearBottom();
    anchor.valid = true;
    if (anchor.wasAtBottom || model->rowCount() == 0) {
        return anchor;
    }

    QModelIndex first = ui->chat_detail_data_list->indexAt(QPoint(2, 2));
    if (!first.isValid()) {
        first = model->index(0, 0);
    }
    anchor.messageId = first.data(MessageListModel::MessageIdRole).toLongLong();
    anchor.clientMessageId = first.data(MessageListModel::ClientMessageIdRole).toString();
    anchor.viewportOffset = ui->chat_detail_data_list->visualRect(first).top();
    return anchor;
}

void ChatPage::saveCurrentScrollAnchor()
{
    if (_currentChatId > 0 && ui->chat_detail_data_list->model()) {
        _scrollAnchors.insert(_currentChatId, captureScrollAnchor());
    }
}

void ChatPage::restoreScrollAnchor(int chatId, const ScrollAnchor &anchor)
{
    QTimer::singleShot(0, this,
        /** @brief 布局完成后仅对原会话及模型恢复滚动锚点。 */
        [this, chatId, anchor]() {
        auto *model = _messageStore.find(chatId);
        if (chatId != _currentChatId || !model
            || ui->chat_detail_data_list->model() != model) {
            return;
        }
        if (anchor.wasAtBottom) {
            ui->chat_detail_data_list->scrollToBottom();
            return;
        }
        const QModelIndex index = model->indexForStableId(anchor.messageId,
                                                          anchor.clientMessageId);
        if (!index.isValid()) {
            return;
        }
        ui->chat_detail_data_list->scrollTo(index, QAbstractItemView::PositionAtTop);
        auto *bar = ui->chat_detail_data_list->verticalScrollBar();
        bar->setValue(bar->value() - anchor.viewportOffset);
    });
}

void ChatPage::queueScrollToBottom(int chatId)
{
    QTimer::singleShot(0, this,
        /** @brief 排队布局结束后仅滚动仍处于当前页的模型。 */
        [this, chatId]() {
        auto *model = _messageStore.find(chatId);
        if (chatId == _currentChatId && model
            && ui->chat_detail_data_list->model() == model) {
            ui->chat_detail_data_list->scrollToBottom();
        }
    });
}

void ChatPage::refreshGroupState()
{
    if (!_chatInfo) return;
    const auto state = UserMgr::instance()->messages()->groupState(_currentChatId);
    const bool group = _chatInfo->getChatType() == ChatType::GROUP;
    const bool active = !group || state["group_state"] == "active";
    ui->send_btn->setEnabled(active); ui->file_label->setEnabled(active); ui->chat_edit->setEnabled(active);
    if (group) ui->title_label->setText(state["name"].toString(_chatInfo->name()) + (active ? QString() : tr("（已离群或解散，只读）")));
    else if (const auto contact=UserMgr::instance()->friendById(_chatInfo->getUid()))
        ui->title_label->setText(contact->_backname.isEmpty() ? contact->_name : contact->_backname);
    ui->file_label->setToolTip(active ? tr("发送图片、视频或文件") : tr("当前群只读"));
}

void ChatPage::openHistorySearch()
{
    if (!_chatInfo) return;
    const int chat = _currentChatId;
    auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("搜索本机已保存历史")); dialog->resize(480,420);
    auto *layout = new QVBoxLayout(dialog); auto *input = new QLineEdit(dialog); input->setMaxLength(200);
    input->setPlaceholderText(tr("当前会话正文或附件名称")); layout->addWidget(input);
    auto *rows = new QListWidget(dialog); layout->addWidget(rows);
    auto *more = new QPushButton(tr("下一页"),dialog); layout->addWidget(more); more->setEnabled(false);
    auto cursor = std::make_shared<qint64>(0); auto term = std::make_shared<QString>();
    auto pending = std::make_shared<bool>(false);
    connect(input,&QLineEdit::returnPressed,dialog,/** @brief 新查询重新开始本地分页。 */ [chat,input,rows,more,cursor,term,pending] {
        *term=input->text().trimmed(); if (term->isEmpty()) return;
        rows->clear(); *cursor=0; *pending=true; more->setEnabled(false);
        UserMgr::instance()->messages()->search(chat,*term);
    });
    connect(more,&QPushButton::clicked,dialog,/** @brief 从上一页末尾继续搜索。 */ [chat,cursor,term,pending,more] {
        if (*pending) return; *pending=true; more->setEnabled(false); UserMgr::instance()->messages()->search(chat,*term,*cursor);
    });
    connect(UserMgr::instance()->messages(),&MessageService::searchLoaded,dialog,
        /** @brief 只接受当前查询的结果，保留完整分页定位标识。 */
        [chat,rows,more,cursor,term,pending](int resultChat,const QString &text,qint64 before,const QVector<StoredMessage> &results) {
        if (resultChat!=chat || text!=*term || before!=*cursor) return;
        *pending=false;
        for (const auto &result : results) {
            const auto visible=result.content.startsWith("@resource:v1:")
                ? QJsonDocument::fromJson(result.content.mid(13).toUtf8()).object()["name"].toString() : result.content;
            auto *item=new QListWidgetItem(visible.left(250),rows);
            item->setData(Qt::UserRole,result.messageId); *cursor=result.localId;
        }
        more->setEnabled(results.size()==50);
    });
    connect(rows,&QListWidget::itemDoubleClicked,dialog,/** @brief 将结果所在历史范围加载到消息视图并请求定位。 */
        [this,chat,dialog](QListWidgetItem *item) {
        if (_currentChatId!=chat) return;
        _searchJumpId=item->data(Qt::UserRole).toLongLong();
        if (_searchJumpId>0) UserMgr::instance()->messages()->loadHistory(chat,_searchJumpId+1);
        dialog->close();
    });
    dialog->show();
}
