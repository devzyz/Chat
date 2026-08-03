#ifndef MESSAGELISTMODEL_H
#define MESSAGELISTMODEL_H

#include "messagerecord.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

class MessageListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        MessageIdRole = Qt::UserRole + 1,
        ClientMessageIdRole,
        ChatIdRole,
        SenderIdRole,
        SenderNameRole,
        AvatarKeyRole,
        AvatarRole,
        SentAtRole,
        DeliveryStatusRole,
        IsSelfRole,
        MessageTypeRole,
        TextRole
    };
    Q_ENUM(Role)

    explicit MessageListModel(int chatId);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int chatId() const;
    const MessageRecord *recordAt(int row) const;

    int appendMessage(const MessageRecord &message);
    int appendMessages(const QVector<MessageRecord> &messages);
    int prependHistory(const QVector<MessageRecord> &messages);

    bool acknowledgeMessage(const QString &clientMessageId, qint64 messageId,
                            DeliveryStatus status = DeliveryStatus::Sent);
    bool updateStatusByClientId(const QString &clientMessageId, DeliveryStatus status);
    bool updateStatusByMessageId(qint64 messageId, DeliveryStatus status);
    bool removeByMessageId(qint64 messageId);

    int rowForClientMessageId(const QString &clientMessageId) const;
    int rowForMessageId(qint64 messageId) const;
    QModelIndex indexForStableId(qint64 messageId, const QString &clientMessageId) const;
    qint64 oldestMessageId() const;

    bool canLoadMore() const;
    void setCanLoadMore(bool canLoadMore);
    bool isLoadingHistory() const;
    void setLoadingHistory(bool loading);
    bool hasLoadedInitialPage() const;
    void setInitialPageLoaded(bool loaded);
    qint64 historyCursor() const;
    void setHistoryCursor(qint64 cursor);

private:
    void assertGuiThread() const;
    bool contains(const MessageRecord &message) const;
    void rebuildRowIndexes();
    bool updateStatusAtRow(int row, DeliveryStatus status);

    int _chatId;
    QVector<MessageRecord> _messages;
    QHash<QString, int> _clientIdRows;
    QHash<qint64, int> _messageIdRows;
    bool _canLoadMore = true;
    bool _loadingHistory = false;
    bool _initialPageLoaded = false;
    qint64 _historyCursor = 0;
};

#endif // MESSAGELISTMODEL_H
