#include "chatpage.h"

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

    ui->receive_btn->SetState("normal", "hover", "press");
    ui->send_btn->SetState("normal", "hover", "press");
    ui->emoij_label->SetState("normal", "hover", "press", "normal", "hover", "press");
    ui->file_label->SetState("normal", "hover", "press", "normal", "hover", "press");

    _messageDelegate = new MessageItemDelegate(ui->chat_detail_data_list);
    ui->chat_detail_data_list->setItemDelegate(_messageDelegate);
    connect(ui->chat_detail_data_list, &ChatDetailList::viewportResized, this, [this]() {
        _messageDelegate->clearSizeCache();
        ui->chat_detail_data_list->doItemsLayout();
        ui->chat_detail_data_list->viewport()->update();
    });
    connect(ui->chat_detail_data_list, &ChatDetailList::nearTopReached,
            this, &ChatPage::requestOlderHistory);
}

ChatPage::~ChatPage()
{
    // QListView does not own the model. Detach it before MessageModelStore is destroyed.
    ui->chat_detail_data_list->setModel(nullptr);
    delete ui;
    ui = nullptr;
}

void ChatPage::SetChatInfo(std::shared_ptr<ChatInfo> chatInfo)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!chatInfo) {
        return;
    }

    saveCurrentScrollAnchor();
    _chatInfo = std::move(chatInfo);
    _currentChatId = _chatInfo->GetChatId();

    if (_chatInfo->GetChatType() == ChatType::PRIVATE) {
        const auto friendInfo = UserMgr::GetInstance()->GetFriendById(_chatInfo->GetUid());
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
    QTimer::singleShot(0, this, [this, selectedChatId]() {
        if (_currentChatId == selectedChatId) {
            _suppressHistoryRequests = false;
        }
    });

    if (!model->hasLoadedInitialPage() && !model->isLoadingHistory()) {
        requestHistory(model);
    }
}

void ChatPage::AppendChatMsg(const std::shared_ptr<ChatDataBase> &message)
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

void ChatPage::ApplyHistoryPage(int chatId,
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

    // The required cursor contract returns ids older than the current first row.
    // Refuse a legacy forward page instead of prepending newer records out of order.
    const qint64 currentOldestId = model->oldestMessageId();
    if (!initialPage && currentOldestId > 0) {
        const bool incompatibleDirection = std::any_of(
            records.cbegin(), records.cend(), [currentOldestId](const MessageRecord &record) {
                return record.messageId > 0 && record.messageId >= currentOldestId;
            });
        if (incompatibleDirection) {
            SPDLOG_WARN("history response direction is incompatible for chat_id={}", chatId);
            model->setCanLoadMore(false);
            model->setLoadingHistory(false);
            if (affectsCurrentView) {
                QTimer::singleShot(0, this, [this, chatId]() {
                    if (_currentChatId == chatId) _suppressHistoryRequests = false;
                });
            }
            return;
        }
    }

    model->prependHistory(records);
    model->setCanLoadMore(canLoadMore);
    model->setHistoryCursor(nextCursor > 0 ? nextCursor : model->oldestMessageId());
    model->setInitialPageLoaded(true);
    model->setLoadingHistory(false);

    if (!affectsCurrentView) {
        return;
    }
    if (initialPage) {
        queueScrollToBottom(chatId);
    } else if (anchor.valid) {
        restoreScrollAnchor(chatId, anchor);
    }
    QTimer::singleShot(0, this, [this, chatId]() {
        if (_currentChatId == chatId) {
            _suppressHistoryRequests = false;
        }
    });
}

void ChatPage::HistoryLoadFailed(int chatId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (auto *model = _messageStore.find(chatId)) {
        model->setLoadingHistory(false);
    }
}

void ChatPage::ApplyDeliveryAcknowledgements(
    int chatId, const QVector<MessageAcknowledgement> &acknowledgements)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto *model = _messageStore.find(chatId);
    if (!model) {
        return;
    }
    for (const auto &acknowledgement : acknowledgements) {
        model->acknowledgeMessage(acknowledgement.clientMessageId,
                                  acknowledgement.messageId,
                                  DeliveryStatus::Sent);
    }
}

void ChatPage::MarkMessagesFailed(int chatId, const QVector<QString> &clientMessageIds)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto *model = _messageStore.find(chatId);
    if (!model) {
        return;
    }
    for (const auto &clientMessageId : clientMessageIds) {
        model->updateStatusByClientId(clientMessageId, DeliveryStatus::Failed);
    }
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

    const auto selfInfo = UserMgr::GetInstance()->GetUserInfo();
    if (!selfInfo) {
        return;
    }

    const QVector<MsgInfo> &messages = ui->chat_edit->getMsgList();
    int textLength = 0;
    QJsonObject textObject;
    QJsonArray textArray;

    auto sendTextBatch = [this, selfInfo, &textObject, &textArray, &textLength]() {
        if (textArray.isEmpty()) {
            return;
        }
        textObject["from_uid"] = selfInfo->_uid;
        textObject["to_uid"] = _chatInfo->GetUid();
        textObject["text_array"] = textArray;
        textObject["chat_id"] = _chatInfo->GetChatId();
        const QByteArray data = QJsonDocument(textObject).toJson(QJsonDocument::Compact);
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, data);
        textLength = 0;
        textArray = QJsonArray();
        textObject = QJsonObject();
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

        const QString uuid = QUuid::createUuid().toString();
        QJsonObject payload;
        payload["msg_content"] = message.content;
        payload["msg_uuid"] = uuid;
        textArray.append(payload);
        textLength += message.content.length();

        auto textMessage = std::make_shared<TextChatData>(
            uuid, _chatInfo->GetChatId(), _chatInfo->GetChatType(),
            ChatMessageType::TEXT_TYPE, message.content, selfInfo->_uid,
            QTime::currentTime());
        AppendChatMsg(textMessage);
        emit sig_append_send_text_cache_msg(uuid, textMessage);
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
    MessageRecord record;
    record.messageId = message->GetMsgId();
    record.clientMessageId = message->GetCacheMsgId();
    record.chatId = message->GetChatId();
    record.senderId = message->GetSendId();
    record.sentAt = message->GetSentAt();
    record.text = message->GetContent();

    switch (message->GetChatMsgType()) {
    case ChatMessageType::TEXT_TYPE: record.messageType = MessageType::Text; break;
    case ChatMessageType::IMAGE_TYPE: record.messageType = MessageType::Image; break;
    case ChatMessageType::FILE_TYPE: record.messageType = MessageType::File; break;
    }

    const auto selfInfo = UserMgr::GetInstance()->GetUserInfo();
    record.isSelf = selfInfo && record.senderId == selfInfo->_uid;
    if (record.isSelf) {
        record.senderName = selfInfo->_name;
        record.avatarKey = selfInfo->_icon;
    } else {
        const auto chatInfo = UserMgr::GetInstance()->GetChatInfo(record.chatId);
        const auto friendInfo = chatInfo
            ? UserMgr::GetInstance()->GetFriendById(chatInfo->GetUid()) : nullptr;
        if (friendInfo) {
            record.senderName = friendInfo->_name;
            record.avatarKey = friendInfo->_icon;
        }
    }
    record.avatar = cachedAvatar(record.avatarKey);

    if (!record.isSelf) {
        record.deliveryStatus = DeliveryStatus::None;
    } else if (message->GetStatus() == ChatStatus::STATUS_SEND_FAILURE) {
        record.deliveryStatus = DeliveryStatus::Failed;
    } else if (record.messageId <= 0 && !record.clientMessageId.isEmpty()) {
        record.deliveryStatus = DeliveryStatus::Sending;
    } else if (message->GetStatus() == ChatStatus::STATUS_READ_ALREADY) {
        record.deliveryStatus = DeliveryStatus::Read;
    } else {
        record.deliveryStatus = DeliveryStatus::Sent;
    }
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
    const auto pending = chatInfo->GetCacheChatMsgs();
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
    emit sig_request_history(model->chatId(), cursor);
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
    QTimer::singleShot(0, this, [this, chatId, anchor]() {
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
    QTimer::singleShot(0, this, [this, chatId]() {
        auto *model = _messageStore.find(chatId);
        if (chatId == _currentChatId && model
            && ui->chat_detail_data_list->model() == model) {
            ui->chat_detail_data_list->scrollToBottom();
        }
    });
}
