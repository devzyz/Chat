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

/** @brief 合并发送和回执状态，保留已经确认的更高回执级别。 */
inline DeliveryStatus mergeDeliveryStatus(DeliveryStatus prior, DeliveryStatus incoming) {
    const auto rank = /** @brief 给已确认状态分级，防止较低等级覆盖更高回执。 */ [](DeliveryStatus state) {
        return state == DeliveryStatus::Read ? 3 : state == DeliveryStatus::Delivered ? 2 :
            state == DeliveryStatus::Sent ? 1 : 0;
    };
    return rank(prior) > rank(incoming) ? prior : incoming;
}

/** @brief 保存消息模型的展示值、稳定身份及状态，不代表服务器持久化事务。 */
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

/** @brief 保存 ACK 中服务器消息 ID 与客户端 UUID 的对应关系及发送状态。 */
struct MessageAcknowledgement {
    QString clientMessageId;
    qint64 messageId = 0;
};

Q_DECLARE_METATYPE(MessageRecord)
Q_DECLARE_METATYPE(MessageAcknowledgement)
Q_DECLARE_METATYPE(QVector<MessageAcknowledgement>)

#endif // MESSAGERECORD_H
