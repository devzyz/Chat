#ifndef MESSAGEITEMDELEGATE_H
#define MESSAGEITEMDELEGATE_H

#include "messagerecord.h"

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>

class QListView;

/** @brief 在 GUI 线程布局和绘制消息气泡、头像及状态，缓存每行尺寸。 */
class MessageItemDelegate final : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于在 GUI 线程布局和绘制消息气泡、头像及状态，缓存每行尺寸。 */
    explicit MessageItemDelegate(QListView *view);
    /** @brief 计算索引对应消息的气泡矩形，供绘制与可见性采样使用。 */
    QRect bubbleRect(const QModelIndex &index, const QRect &rowRect) const;

    /** @brief 在给定区域绘制消息气泡、头像及状态，不修改持久化数据。 */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    /** @brief 返回当前条目建议尺寸，供列表布局使用。 */
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

public slots:
    /** @brief 清空消息尺寸缓存，使后续布局重新计算。 */
    void clearSizeCache();

private:
    /** @brief 保存单条消息气泡、头像和文本的布局尺寸。 */
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

    /** @brief 从模型角色恢复用于绘制的消息值。 */
    MessageRecord recordFromIndex(const QModelIndex &index) const;
    /** @brief 根据消息类型、文本和可用宽度计算布局尺寸。 */
    Metrics calculateMetrics(const MessageRecord &message, int width) const;
    /** @brief 生成区分消息内容和可用宽度的尺寸缓存键。 */
    QString sizeCacheKey(const MessageRecord &message, int width) const;

    QListView *_view;
    QPixmap _sendingPixmap;
    QPixmap _failedPixmap;
    QPixmap _readPixmap;
    mutable QHash<QString, QSize> _sizeCache;
};

#endif // MESSAGEITEMDELEGATE_H
