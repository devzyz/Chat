#include "avatarcropwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

AvatarCropWidget::AvatarCropWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(280, 280);
    setCursor(Qt::OpenHandCursor);
    setObjectName(QStringLiteral("avatarCropWidget"));
}

void AvatarCropWidget::setImage(const QImage &image)
{
    _image = image;
    _crop.setImageSize(image.size());
    _dragging = false;
    setCursor(Qt::OpenHandCursor);
    emit zoomChanged(_crop.zoom());
    update();
}

void AvatarCropWidget::setZoom(int percent)
{
    _crop.setZoom(percent);
    emit zoomChanged(_crop.zoom());
    update();
}

QRectF AvatarCropWidget::frameRect() const
{
    const qreal diameter = qMin(width(), height()) - 32.0;
    return {(width() - diameter) / 2, (height() - diameter) / 2, diameter, diameter};
}

/** @brief 绘制原图、裁剪范围与遮罩。 */
void AvatarCropWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(32, 34, 38));
    const QRectF frame = frameRect();
    if (!_image.isNull()) {
        const QRectF source = _crop.sourceRect();
        const qreal scale = frame.width() / source.width();
        const QRectF target(frame.topLeft() - source.topLeft() * scale, QSizeF(_image.size()) * scale);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, _image);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shade;
    shade.addRect(rect());
    shade.addEllipse(frame);
    painter.fillPath(shade, QColor(0, 0, 0, 150));
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(frame);
}

void AvatarCropWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !_image.isNull()) {
        _dragging = true;
        _lastPosition = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void AvatarCropWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (_dragging) {
        const qreal sourcePerPixel = _crop.sourceRect().width() / frameRect().width();
        _crop.moveBy((_lastPosition - event->position()) * sourcePerPixel);
        _lastPosition = event->position();
        update();
        event->accept();
    }
}

void AvatarCropWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        _dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    }
}

void AvatarCropWidget::wheelEvent(QWheelEvent *event)
{
    if (!_image.isNull()) {
        setZoom(_crop.zoom() + event->angleDelta().y() / 12);
        event->accept();
    }
}
