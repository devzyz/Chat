#include "clientmessage.h"
#include "usermgr.h"

MessageRecord clientMessageRecord(const std::shared_ptr<ChatDataBase> &message)
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
