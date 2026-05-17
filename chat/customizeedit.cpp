#include "customizeedit.h"

CustomizeEdit::CustomizeEdit(QWidget *parent) : QLineEdit(parent), _max_len(0) {
    connect(this, &QLineEdit::textChanged, this, &CustomizeEdit::limitTextLength);
}

void CustomizeEdit::SetMaxLength(int maxLen) {
    _max_len = maxLen;
}

void CustomizeEdit::focusOutEvent(QFocusEvent * event) {
    // 发出失去焦点信号，调用基类的失去焦点
    QLineEdit::focusOutEvent(event);
    emit sig_focus_out();
}

// 最大长度限制
void CustomizeEdit::limitTextLength(QString text) {
    if (_max_len <= 0) {
        return ;
    }
    QByteArray byteArray = text.toUtf8();

    // 按照字节流进行比较，当超过约定的字节后，进行截取
    if (byteArray.size() > _max_len) {
        byteArray = byteArray.left(_max_len);
        this->setText(QString::fromUtf8(byteArray));
    }
}
