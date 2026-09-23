#include "clientmessage.h"
#include "usermgr.h"

MessageRecord clientMessageRecord(const std::shared_ptr<ChatDataBase> &message)
{
    MessageRecord record;
    record.messageId = message->getMsgId();
    record.clientMessageId = message->getCacheMsgId();
    record.chatId = message->getChatId();
    record.senderId = message->getSendId();
    record.sentAt = message->getSentAt();
    record.text = message->getContent();

    switch (message->getChatMsgType()) {
    case ChatMessageType::TEXT_TYPE: record.messageType = MessageType::Text; break;
    case ChatMessageType::IMAGE_TYPE: record.messageType = MessageType::Image; break;
    case ChatMessageType::FILE_TYPE: record.messageType = MessageType::File; break;
    }

    const auto selfInfo = UserMgr::instance()->userInfo();
    record.isSelf = selfInfo && record.senderId == selfInfo->_uid;
    if (record.isSelf) {
        record.senderName = selfInfo->_name;
        record.avatarKey = selfInfo->_icon;
    } else {
        const auto chatInfo = UserMgr::instance()->chatInfo(record.chatId);
        const auto friendInfo = chatInfo
            ? UserMgr::instance()->friendById(chatInfo->getUid()) : nullptr;
        if (friendInfo) {
            record.senderName = friendInfo->_name;
            record.avatarKey = friendInfo->_icon;
        }
    }

    if (!record.isSelf) {
        record.deliveryStatus = DeliveryStatus::None;
    } else if (message->getStatus() == ChatStatus::STATUS_SEND_FAILURE) {
        record.deliveryStatus = DeliveryStatus::Failed;
    } else if (record.messageId <= 0 && !record.clientMessageId.isEmpty()) {
        record.deliveryStatus = DeliveryStatus::Sending;
    } else {
        record.deliveryStatus = DeliveryStatus::Sent;
    }
    return record;
}
