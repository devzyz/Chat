#pragma once

#include "avatarcrop.h"

#include <QWidget>

class AvatarCropWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit AvatarCropWidget(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void setZoom(int percent);
    int zoom() const { return _crop.zoom(); }
    QRectF sourceRect() const { return _crop.sourceRect(); }
    QRectF frameRect() const;

signals:
    void zoomChanged(int percent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    AvatarCrop _crop;
    QImage _image;
    QPointF _lastPosition;
    bool _dragging = false;
};
