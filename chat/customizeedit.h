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
    CustomizeEdit(QWidget *parent = nullptr);
    void SetMaxLength(int maxLen);

protected:
    // 重写基类的焦点失去
    void focusOutEvent(QFocusEvent * event) override;

private:
    void limitTextLength(QString text);

    int _max_len;

signals:
    // 发出失去焦点信号
    void sig_focus_out();
};

#endif // CUSTOMIZEEDIT_H
