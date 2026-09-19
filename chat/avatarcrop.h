#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>

class AvatarCrop
{
public:
    void setImageSize(QSize size);
    void setZoom(int percent);
    void moveBy(QPointF sourceDelta);
    int zoom() const { return _zoom; }
    QRectF sourceRect() const;
    static QImage render(const QImage &image, const QRectF &sourceRect);
    static QImage circularPreview(const QImage &image);

private:
    void clampCenter();
    QSize _size;
    QPointF _center;
    int _zoom = 100;
};
