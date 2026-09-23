#ifndef CLICKEDONCELABEL_H
#define CLICKEDONCELABEL_H

#include <QLabel>
#include <QMouseEvent>

/** @brief 将一次鼠标释放转换为点击事件的标签控件。 */
class ClickedOnceLabel : public QLabel
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于将一次鼠标释放转换为点击事件的标签控件。 */
    ClickedOnceLabel(QWidget * parent = nullptr);
    /** @brief 处理鼠标释放并按控件状态触发点击或结束拖动。 */
    virtual void mouseReleaseEvent(QMouseEvent * event) override;

signals:
    /** @brief 通知用户完成一次有效点击。 */
    void clicked(QString);
};

#endif // CLICKEDONCELABEL_H
