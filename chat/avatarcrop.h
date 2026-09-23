#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>

/** @brief 保存头像裁剪中心和缩放比例，并将有效裁剪区域约束在源图内。 */
class AvatarCrop
{
public:
    /** @brief 设置源图尺寸并重置裁剪中心及缩放约束。 */
    void setImageSize(QSize size);
    /** @brief 设置百分比缩放并将裁剪中心限制在有效范围。 */
    void setZoom(int percent);
    /** @brief 按源图坐标偏移裁剪中心，并限制裁剪框不越界。 */
    void moveBy(QPointF sourceDelta);
    /** @brief 返回当前裁剪缩放百分比。 */
    int zoom() const { return _zoom; }
    /** @brief 返回当前裁剪区域在源图中的矩形。 */
    QRectF sourceRect() const;
    /** @brief 按源图区域生成标准头像图片值。 */
    static QImage render(const QImage &image, const QRectF &sourceRect);
    /** @brief 为头像生成带透明圆角遮罩的圆形预览。 */
    static QImage circularPreview(const QImage &image);

private:
    /** @brief 将裁剪中心夹到当前缩放下仍覆盖完整裁剪区域的范围。 */
    void clampCenter();
    QSize _size;
    QPointF _center;
    int _zoom = 100;
};
