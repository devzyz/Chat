#pragma once

#include "avatarcrop.h"

#include <QWidget>

/** @brief 展示头像裁剪预览并将鼠标拖动和滚轮操作转换为裁剪参数。 */
class AvatarCropWidget final : public QWidget
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于展示头像裁剪预览并将鼠标拖动和滚轮操作转换为裁剪参数。 */
    explicit AvatarCropWidget(QWidget *parent = nullptr);
    /** @brief 设置头像预览源图并更新裁剪状态。 */
    void setImage(const QImage &image);
    /** @brief 设置百分比缩放并将裁剪中心限制在有效范围。 */
    void setZoom(int percent);
    /** @brief 返回当前裁剪缩放百分比。 */
    int zoom() const { return _crop.zoom(); }
    /** @brief 返回当前裁剪区域在源图中的矩形。 */
    QRectF sourceRect() const { return _crop.sourceRect(); }
    /** @brief 返回控件中的裁剪框区域。 */
    QRectF frameRect() const;

signals:
    /** @brief 通知拖动或滚轮操作后的缩放百分比。 */
    void zoomChanged(int percent);

protected:
    /** @brief 绘制当前控件的背景、边框或内容，遵循 Qt GUI 线程事件约束。 */
    void paintEvent(QPaintEvent *event) override;
    /** @brief 处理鼠标按下，更新控件当前交互状态。 */
    void mousePressEvent(QMouseEvent *event) override;
    /** @brief 处理鼠标移动，更新当前拖动的裁剪位置。 */
    void mouseMoveEvent(QMouseEvent *event) override;
    /** @brief 处理鼠标释放并按控件状态触发点击或结束拖动。 */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /** @brief 处理滚轮并按缩放限制更新头像裁剪比例。 */
    void wheelEvent(QWheelEvent *event) override;

private:
    AvatarCrop _crop;
    QImage _image;
    QPointF _lastPosition;
    bool _dragging = false;
};
