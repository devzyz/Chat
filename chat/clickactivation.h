#pragma once
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWidget>
/** @brief 保存一次鼠标或键盘激活意图；取消释放不触发业务动作。 */
class ClickActivation {
public:
    /** @brief 只记录控件内左键按下。 */
    void press(QWidget *widget, QMouseEvent *event) {
        _pressed = event->button() == Qt::LeftButton && widget->rect().contains(event->position().toPoint());
    }
    /** @brief 消费一次有效释放，拖出或其他按钮均不激活。 */
    bool release(QWidget *widget, QMouseEvent *event) {
        const bool activate = _pressed && event->button() == Qt::LeftButton
            && widget->rect().contains(event->position().toPoint());
        _pressed = false; return activate;
    }
    /** @brief 记录非自动重复的空格或回车按下。 */
    bool keyPress(QKeyEvent *event) {
        const bool key = event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
        if (key && !event->isAutoRepeat()) _key = event->key();
        return key;
    }
    /** @brief 消费匹配的键盘释放。 */
    bool keyRelease(QKeyEvent *event) {
        if (event->isAutoRepeat()) return false;
        const bool activate = _key != 0 && _key == event->key(); _key = 0; return activate;
    }
    /** @brief 失焦取消尚未完成的激活。 */
    void cancel() { _pressed = false; _key = 0; }
private:
    bool _pressed = false;
    int _key = 0;
};
