#include "textchatbubble.h"
#include <QEvent>
#include <QTextBlock>
#include <QFontMetricsF>
#include <QVBoxLayout>

TextChatBubble::TextChatBubble(ChatRole role, const QString &text, const QString& userName, const QString& userIcon, QWidget *parent)
    : ChatBubble(role, parent)
{
    // 设置用户的头像和昵称
    setUserName(userName);
    setUserIcon(userIcon);

    // 创建文字填充区域
    _text_edit = new QTextEdit();
    _text_edit->setReadOnly(true);
    _text_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _text_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _text_edit->installEventFilter(this);
    _text_edit->setFrameShape(QFrame::NoFrame);  // 去除边框
    // 文本字体
    QFont font("Microsoft YaHei");
    font.setPixelSize(16);
    _text_edit->setFont(font);
    _text_edit->setStyleSheet("QTextEdit{background: transparent; border: none}");

    // 创建交换区
    _wrapper = new BubbleFrame(role);
    _wrapper->setTextWidget(_text_edit);

    setPlainText(text);
    setWidget(_wrapper);
}

/**
 * @brief TextChatBubble::setPlainText
 * @param text
 * @param wrapper
 * 将文本信息填充进去，同时设置高度等信息
 */
void TextChatBubble::setPlainText(const QString &text)
{
    // 设置文本的内容
    _text_edit->setPlainText(text);
    _text_edit->document()->setDocumentMargin(0);

    QTextDocument doc;
    doc.setDefaultFont(_text_edit->font()); // 设置字体信息
    doc.setDocumentMargin(0);
    doc.setPlainText(text); // 设置文本内容

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); // 设置换行模式
    doc.setDefaultTextOption(option);

    // 文本区域最大宽度，不包含左右边距和三角
    // 400为整个气泡的最大宽度
    // 两个3分别为气泡与文本之间的左右间隔
    // 8为气泡的宽度
    int maxTextWidth = 400 - 6 - 6 - 8;

    // 自然宽度
    qreal naturalTextWidth = doc.idealWidth();

    // 限制最大宽度
    qreal finalTextWidth = qMin(naturalTextWidth, qreal(maxTextWidth));
    doc.setTextWidth(finalTextWidth);

    // 设置textWidth会自动更新为正确大小
    QSizeF textSize = doc.size();

    int textWidgetWidth = qCeil(textSize.width());
    int textWidgetHeight = qCeil(textSize.height());

    int bubbleWidth = textWidgetWidth + 4 + 4 + 8;

    int bubbleHeight = qMax(10, textWidgetHeight + 4 + 4);

    // 设置尺寸大小
    _text_edit->setFixedSize(textWidgetWidth, textWidgetHeight);
    _wrapper->setFixedSize(bubbleWidth, bubbleHeight);
}
