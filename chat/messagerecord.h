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
    Read
};

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
    MessageType messageType = MessageType::Text;
    QString text;
};

struct MessageAcknowledgement {
    QString clientMessageId;
    qint64 messageId = 0;
};

Q_DECLARE_METATYPE(MessageRecord)
Q_DECLARE_METATYPE(MessageAcknowledgement)
Q_DECLARE_METATYPE(QVector<MessageAcknowledgement>)

#endif // MESSAGERECORD_H
