#ifndef MESSAGERECORD_H
#define MESSAGERECORD_H

#include <QDateTime>
#include <QMetaType>
#include <QPixmap>
#include <QString>
#include <QVector>

enum class MessageType {
    Text,
    Image,
    Video,
    File
};

enum class DeliveryStatus {
    None,
    Sending,
    Sent,
    Failed,
    Read,
    Uncertain,
    Queued,
    Delivered
};

inline DeliveryStatus mergeDeliveryStatus(DeliveryStatus prior, DeliveryStatus incoming) {
    const auto rank = [](DeliveryStatus state) {
        return state == DeliveryStatus::Read ? 3 : state == DeliveryStatus::Delivered ? 2 :
            state == DeliveryStatus::Sent ? 1 : 0;
    };
    return rank(prior) > rank(incoming) ? prior : incoming;
}

struct MessageRecord {
    qint64 messageId = 0;
    QString clientMessageId;
    int chatId = 0;
    int senderId = 0;
    QString senderName;
    QString avatarKey;
    QPixmap avatar;
    QDateTime sentAt;
    DeliveryStatus deliveryStatus = DeliveryStatus::None;
    bool isSelf = false;
    bool durable = false;
    bool readConfirmed = false;
    MessageType messageType = MessageType::Text;
    QString text;
    QString resourceId;
    QString localResourcePath;
    QPixmap resourcePreview;
};

struct MessageAcknowledgement {
    QString clientMessageId;
    qint64 messageId = 0;
};

Q_DECLARE_METATYPE(MessageRecord)
Q_DECLARE_METATYPE(MessageAcknowledgement)
Q_DECLARE_METATYPE(QVector<MessageAcknowledgement>)

#endif // MESSAGERECORD_H
