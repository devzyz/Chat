#include "messageitemdelegate.h"

#include "messagelistmodel.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QTextOption>
#include <QtMath>

namespace {
constexpr int kOuterMargin = 8;
constexpr int kAvatarSize = 42;
constexpr int kAvatarTop = 8;
constexpr int kAvatarGap = 8;
constexpr int kNameHeight = 18;
constexpr int kBubbleTop = 28;
constexpr int kBubblePadding = 4;
constexpr int kTriangleWidth = 8;
constexpr int kStatusSize = 15;
constexpr int kTimeHeight = 14;
constexpr int kBottomMargin = 8;
constexpr int kMaximumBubbleWidth = 400;

QFont textFont()
{
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(16);
    return font;
}

QFont nameFont()
{
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(9);
    return font;
}

QFont timeFont()
{
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(10);
    return font;
}

QSize textDocumentSize(const QString &text, int maximumWidth)
{
    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultFont(textFont());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    document.setPlainText(text);

    const int naturalWidth = qMax(1, qCeil(document.idealWidth()));
    document.setTextWidth(qMin(naturalWidth, maximumWidth));
    const QSizeF size = document.size();
    return {qMax(1, qCeil(size.width())), qMax(1, qCeil(size.height()))};
}
}

MessageItemDelegate::MessageItemDelegate(QListView *view)
    : QStyledItemDelegate(view), _view(view),
      _sendingPixmap(QStringLiteral(":/res/status_no_read.png")),
      _failedPixmap(QStringLiteral(":/res/status_send_failure.png")),
      _readPixmap(QStringLiteral(":/res/status_already_read.png"))
{
}

void MessageItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    const MessageRecord message = recordFromIndex(index);
    const Metrics metrics = calculateMetrics(message, option.rect.width());
    const QPoint origin = option.rect.topLeft();

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                            QPainter::SmoothPixmapTransform);
    painter->translate(origin);

    if (option.state.testFlag(QStyle::State_Selected)) {
        painter->fillRect(QRect(QPoint(0, 0), QSize(metrics.itemWidth, metrics.itemHeight)),
                          QColor(0, 0, 0, 10));
    } else if (option.state.testFlag(QStyle::State_MouseOver)) {
        painter->fillRect(QRect(QPoint(0, 0), QSize(metrics.itemWidth, metrics.itemHeight)),
                          QColor(255, 255, 255, 28));
    }

    if (!message.avatar.isNull()) {
        painter->drawPixmap(metrics.avatarRect,
                            message.avatar.scaled(metrics.avatarRect.size(), Qt::KeepAspectRatioByExpanding,
                                                  Qt::SmoothTransformation));
    } else {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(205, 208, 212));
        painter->drawEllipse(metrics.avatarRect);
        painter->setPen(QColor(90, 90, 90));
        painter->setFont(nameFont());
        painter->drawText(metrics.avatarRect, Qt::AlignCenter,
                          message.senderName.left(1).toUpper());
    }

    painter->setFont(nameFont());
    painter->setPen(QColor(0, 0, 0));
    painter->drawText(metrics.nameRect,
                      (message.isSelf ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                      message.senderName);

    const QColor bubbleColor = message.isSelf ? QColor(158, 234, 106) : QColor(Qt::white);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bubbleColor);
    painter->drawRoundedRect(metrics.bubbleBodyRect, 5, 5);
    painter->drawPolygon(metrics.triangle);

    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultFont(textFont());
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(textOption);
    document.setPlainText(message.text);
    document.setTextWidth(metrics.textRect.width());
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, QColor(0, 0, 0));
    painter->save();
    painter->translate(metrics.textRect.topLeft());
    painter->setClipRect(QRect(QPoint(0, 0), metrics.textRect.size()));
    document.documentLayout()->draw(painter, context);
    painter->restore();

    painter->setFont(timeFont());
    painter->setPen(QColor(140, 140, 140));
    painter->drawText(metrics.timeRect,
                      (message.isSelf ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                      message.sentAt.isValid() ? message.sentAt.toString(QStringLiteral("HH:mm")) : QString());

    if (const QPixmap *status = statusPixmap(message.deliveryStatus)) {
        painter->drawPixmap(metrics.statusRect,
                            status->scaled(metrics.statusRect.size(), Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
    }
    painter->restore();
}

QSize MessageItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    const MessageRecord message = recordFromIndex(index);
    int width = option.rect.width();
    if (width <= 0 && _view && _view->viewport()) {
        width = _view->viewport()->width();
    }
    width = qMax(width, 160);
    const QString key = sizeCacheKey(message, width);
    const auto found = _sizeCache.constFind(key);
    if (found != _sizeCache.cend()) {
        return found.value();
    }
    const Metrics metrics = calculateMetrics(message, width);
    const QSize size(metrics.itemWidth, metrics.itemHeight);
    _sizeCache.insert(key, size);
    return size;
}

void MessageItemDelegate::clearSizeCache()
{
    _sizeCache.clear();
}

MessageRecord MessageItemDelegate::recordFromIndex(const QModelIndex &index) const
{
    MessageRecord message;
    message.messageId = index.data(MessageListModel::MessageIdRole).toLongLong();
    message.clientMessageId = index.data(MessageListModel::ClientMessageIdRole).toString();
    message.chatId = index.data(MessageListModel::ChatIdRole).toInt();
    message.senderId = index.data(MessageListModel::SenderIdRole).toInt();
    message.senderName = index.data(MessageListModel::SenderNameRole).toString();
    message.avatarKey = index.data(MessageListModel::AvatarKeyRole).toString();
    message.avatar = qvariant_cast<QPixmap>(index.data(MessageListModel::AvatarRole));
    message.sentAt = index.data(MessageListModel::SentAtRole).toDateTime();
    message.deliveryStatus = static_cast<DeliveryStatus>(index.data(MessageListModel::DeliveryStatusRole).toInt());
    message.isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    message.messageType = static_cast<MessageType>(index.data(MessageListModel::MessageTypeRole).toInt());
    message.text = index.data(MessageListModel::TextRole).toString();
    return message;
}

MessageItemDelegate::Metrics MessageItemDelegate::calculateMetrics(const MessageRecord &message,
                                                                   int width) const
{
    Metrics metrics;
    metrics.itemWidth = qMax(width, 160);
    const int contentLimit = qMax(56, metrics.itemWidth - (2 * kOuterMargin + kAvatarSize + kAvatarGap));
    const int bubbleLimit = qMin(kMaximumBubbleWidth,
                                 qMin(qMax(80, qRound(metrics.itemWidth * 0.63)),
                                      contentLimit));
    const int maximumTextWidth = qMax(32, bubbleLimit - kTriangleWidth - 2 * kBubblePadding);
    const QSize textSize = textDocumentSize(message.text, maximumTextWidth);
    const int bubbleWidth = textSize.width() + 2 * kBubblePadding + kTriangleWidth;
    const int bubbleHeight = textSize.height() + 2 * kBubblePadding;
    const int avatarX = message.isSelf
        ? metrics.itemWidth - kOuterMargin - kAvatarSize
        : kOuterMargin;
    metrics.avatarRect = QRect(avatarX, kAvatarTop, kAvatarSize, kAvatarSize);

    int bodyX = 0;
    if (message.isSelf) {
        const int overallRight = metrics.avatarRect.left() - kAvatarGap;
        bodyX = overallRight - bubbleWidth;
        metrics.bubbleBodyRect = QRect(bodyX, kBubbleTop,
                                       bubbleWidth - kTriangleWidth, bubbleHeight);
        const int right = metrics.bubbleBodyRect.right() + 1;
        metrics.triangle = QPolygon({QPoint(right, kBubbleTop + 12),
                                     QPoint(right, kBubbleTop + 20),
                                     QPoint(right + kTriangleWidth, kBubbleTop + 16)});
        metrics.textRect = metrics.bubbleBodyRect.adjusted(kBubblePadding, kBubblePadding,
                                                           -kBubblePadding, -kBubblePadding);
        metrics.nameRect = QRect(metrics.bubbleBodyRect.left(), 4,
                                 metrics.bubbleBodyRect.width(), kNameHeight);
        metrics.statusRect = QRect(metrics.bubbleBodyRect.left() - kStatusSize - 6,
                                   metrics.bubbleBodyRect.bottom() - kStatusSize + 1,
                                   kStatusSize, kStatusSize);
    } else {
        const int overallLeft = metrics.avatarRect.right() + 1 + kAvatarGap;
        bodyX = overallLeft + kTriangleWidth;
        metrics.bubbleBodyRect = QRect(bodyX, kBubbleTop,
                                       bubbleWidth - kTriangleWidth, bubbleHeight);
        const int left = metrics.bubbleBodyRect.left();
        metrics.triangle = QPolygon({QPoint(left, kBubbleTop + 12),
                                     QPoint(left, kBubbleTop + 20),
                                     QPoint(left - kTriangleWidth, kBubbleTop + 16)});
        metrics.textRect = metrics.bubbleBodyRect.adjusted(kBubblePadding, kBubblePadding,
                                                           -kBubblePadding, -kBubblePadding);
        metrics.nameRect = QRect(metrics.bubbleBodyRect.left(), 4,
                                 metrics.bubbleBodyRect.width(), kNameHeight);
    }

    const int timeTop = metrics.bubbleBodyRect.bottom() + 3;
    metrics.timeRect = QRect(metrics.bubbleBodyRect.left(), timeTop,
                             metrics.bubbleBodyRect.width(), kTimeHeight);
    metrics.itemHeight = qMax(metrics.avatarRect.bottom() + 1,
                              metrics.timeRect.bottom() + 1) + kBottomMargin;
    return metrics;
}

QString MessageItemDelegate::sizeCacheKey(const MessageRecord &message, int width) const
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(message.chatId)
        .arg(message.messageId)
        .arg(message.clientMessageId)
        .arg(width)
        .arg(qHash(message.text))
        .arg(qHash(message.senderName));
}

const QPixmap *MessageItemDelegate::statusPixmap(DeliveryStatus status) const
{
    switch (status) {
    case DeliveryStatus::Sending:
    case DeliveryStatus::Sent: return &_sendingPixmap;
    case DeliveryStatus::Failed: return &_failedPixmap;
    case DeliveryStatus::Read: return &_readPixmap;
    case DeliveryStatus::None: return nullptr;
    }
    return nullptr;
}
