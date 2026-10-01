#ifndef CUSTOMIZEEDIT_H
#define CUSTOMIZEEDIT_H

#include <QLineEdit>

/**
 * @brief The CustomizeEdit class
 * 自定义的edit编辑框，带有最大长度限制
 */
class CustomizeEdit : public QLineEdit
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于限制输入长度并在失去焦点时通知表单校验。 */
    CustomizeEdit(QWidget *parent = nullptr);


protected:
    // 重写基类的焦点失去
    /** @brief 响应输入框失焦并通知外部执行校验。 */
    void focusOutEvent(QFocusEvent * event) override;

signals:
    // 发出失去焦点信号
    /** @brief 发出失去焦点信号。 */
    void focusLost();
};

#endif // CUSTOMIZEEDIT_H
