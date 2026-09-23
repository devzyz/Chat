#include "chatpage.h"
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
    if (_currentChatId != chatInfo->getChatId()) _readTracker->resetExposure();
    _chatInfo = std::move(chatInfo);
    _currentChatId = _chatInfo->getChatId();

    if (_chatInfo->getChatType() == ChatType::PRIVATE) {
        const auto friendInfo = UserMgr::instance()->friendById(_chatInfo->getUid());
        if (friendInfo) {
            ui->title_label->setText(friendInfo->_name);
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
        if (initial || anchor.wasAtBottom) queueScrollToBottom(chatId);
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
    if (!_chatInfo) {
        SPDLOG_WARN("send ignored because chat information is empty");
        return;
    }

    const auto selfInfo = UserMgr::instance()->userInfo();
    if (!selfInfo) {
        return;
    }

    const QVector<MsgInfo> &messages = ui->chat_edit->getMsgList();
    int textLength = 0;
    QJsonArray textArray;

    auto sendTextBatch =
        /** @brief 同步打包当前文本数组并发送，引用捕获不离开本次函数调用。 */
        [this, selfInfo, &textArray, &textLength]() {
        if (textArray.isEmpty()) {
            return;
        }
        const QByteArray data = clientTextRequest(selfInfo->_uid, _chatInfo->getUid(),
                                                  _chatInfo->getChatId(), textArray);
        UserMgr::instance()->messages()->send(QJsonDocument::fromJson(data).object());
        textLength = 0;
        textArray = QJsonArray();
    };

    for (const auto &message : messages) {
        if (message.msgFlag != QStringLiteral("text") || message.content.isEmpty()
            || message.content.length() > 1024) {
            // Image/video/file records are represented by MessageType but upload is not part of this phase.
            continue;
        }

        if (textLength + message.content.length() > 1024) {
            sendTextBatch();
        }

        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject payload;
        payload["msg_content"] = message.content;
        payload["msg_uuid"] = uuid;
        textArray.append(payload);
        textLength += message.content.length();

        auto textMessage = std::make_shared<TextChatData>(
            uuid, _chatInfo->getChatId(), _chatInfo->getChatType(),
            ChatMessageType::TEXT_TYPE, message.content, selfInfo->_uid,
            QTime::currentTime());
        appendChatMsg(textMessage);
        emit outgoingTextQueued(uuid, textMessage);
    }

    sendTextBatch();
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
