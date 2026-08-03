#ifndef MESSAGEITEMDELEGATE_H
#define MESSAGEITEMDELEGATE_H

#include "messagerecord.h"

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>

class QListView;

class MessageItemDelegate final : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MessageItemDelegate(QListView *view);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

public slots:
    void clearSizeCache();

private:
    struct Metrics {
        int itemWidth = 0;
        int itemHeight = 0;
        QRect avatarRect;
        QRect nameRect;
        QRect bubbleBodyRect;
        QRect textRect;
        QRect timeRect;
        QRect statusRect;
        QPolygon triangle;
    };

    MessageRecord recordFromIndex(const QModelIndex &index) const;
    Metrics calculateMetrics(const MessageRecord &message, int width) const;
    QString sizeCacheKey(const MessageRecord &message, int width) const;
    const QPixmap *statusPixmap(DeliveryStatus status) const;

    QListView *_view;
    QPixmap _sendingPixmap;
    QPixmap _failedPixmap;
    QPixmap _readPixmap;
    mutable QHash<QString, QSize> _sizeCache;
};

#endif // MESSAGEITEMDELEGATE_H
