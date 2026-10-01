#ifndef CLICKEDONCELABEL_H
#define CLICKEDONCELABEL_H

#include <QLabel>
#include "clickactivation.h"
#include <QMouseEvent>

/** @brief 将一次鼠标释放转换为点击事件的标签控件。 */
class ClickedOnceLabel : public QLabel
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于将一次鼠标释放转换为点击事件的标签控件。 */
    ClickedOnceLabel(QWidget * parent = nullptr);
    /** @brief 记录控件内左键按下。 */
    void mousePressEvent(QMouseEvent *event) override;
    /** @brief 在控件内完成左键释放才发出点击。 */
    virtual void mouseReleaseEvent(QMouseEvent * event) override;

protected:
    /** @brief 记录键盘激活，不在按下时改变业务状态。 */
    void keyPressEvent(QKeyEvent *event) override;
    /** @brief 有效键盘释放触发一次操作。 */
    void keyReleaseEvent(QKeyEvent *event) override;
    /** @brief 失焦取消未完成的操作。 */
    void focusOutEvent(QFocusEvent *event) override;
private:
    ClickActivation _activation;
signals:
    /** @brief 通知用户完成一次有效点击。 */
    void clicked(QString);
};

#endif // CLICKEDONCELABEL_H
