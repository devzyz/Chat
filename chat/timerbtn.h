#ifndef TIMERBTN_H
#define TIMERBTN_H
#include <QPushButton>
#include <QTimer>

/** @brief 发送验证码后按倒计时更新按钮文字并限制重复操作。 */
class TimerBtn : public QPushButton
{
public:
    /** @brief 创建发送验证码后按倒计时更新按钮文字并限制重复操作。 */
    TimerBtn(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~TimerBtn();
private:
    int _counter;
    QTimer* _timer;
};

#endif // TIMERBTN_H
