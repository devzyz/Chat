#include "avatarcrop.h"

#include <QPainter>

void AvatarCrop::setImageSize(QSize size)
{
    _size = size;
    _center = QPointF(size.width() / 2.0, size.height() / 2.0);
    _zoom = 100;
}

QRectF AvatarCrop::sourceRect() const
{
    if (_size.isEmpty()) {
        return {};
    }
    const qreal side = qMin(_size.width(), _size.height()) * 100.0 / _zoom;
    return {_center.x() - side / 2, _center.y() - side / 2, side, side};
}

void AvatarCrop::clampCenter()
{
    if (_size.isEmpty()) {
        return;
    }
    const qreal halfSide = sourceRect().width() / 2;
    _center.setX(qBound(halfSide, _center.x(), _size.width() - halfSide));
    _center.setY(qBound(halfSide, _center.y(), _size.height() - halfSide));
}

void AvatarCrop::setZoom(int percent)
{
    _zoom = qBound(100, percent, 400);
    clampCenter();
}

void AvatarCrop::moveBy(QPointF sourceDelta)
{
    _center += sourceDelta;
    clampCenter();
}

QImage AvatarCrop::render(const QImage &image, const QRectF &sourceRect)
{
    if (image.isNull() || sourceRect.isEmpty() || !QRectF(image.rect()).contains(sourceRect)) {
        return {};
    }
    QImage result(256, 256, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(result.rect()), image, sourceRect);
    return result;
}

QImage AvatarCrop::circularPreview(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }
    QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(image));
    painter.drawEllipse(QRectF(result.rect()));
    return result;
}
