#include "global.h"

std::function<void(QWidget*)> repolish =
    /** @brief 重新应用控件样式，使动态属性变化生效。 */
    [](QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
};

QString gate_url_prefix = "";

std::function<QString(QString)> xorString =
    /** @brief 保留现有按字符串长度异或的协议转换规则。 */
    [](QString input) {
    QString result = input; // 复制原始字符串
    int length = input.length();
    length = length % 256;
    for (int i = 0; i < input.length(); i ++ ) {
        result[i] = QChar(static_cast<uchar>(input[i].unicode() ^ static_cast<uchar> (length)));
    }
    return result;
};
