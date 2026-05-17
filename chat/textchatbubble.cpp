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
    _text_edit->setMaximumWidth(400); // 最大宽度
    _text_edit->setFrameShape(QFrame::NoFrame);  // 去除边框

    // 文本字体
    QFont font("Microsoft YaHei");
    font.setPixelSize(16);
    _text_edit->setFont(font);
    // 创建交换区
    QWidget *wrapper = new QWidget();
    wrapper->setObjectName("text_bubble_wrapper");
    QVBoxLayout *layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(0);
    layout->addWidget(_text_edit);

    // 设置文本的对齐方式
    if (role == ChatRole::Other) {
        layout->addWidget(_text_edit, 0, Qt::AlignLeft);
    } else {
        layout->addWidget(_text_edit, 0, Qt::AlignRight);
    }

    setPlainText(text, wrapper);
    initStyleSheet(wrapper);
    setWidget(wrapper);
}

/**
 * @brief TextChatBubble::setPlainText
 * @param text
 * @param wrapper
 * 将文本信息填充进去，同时设置高度等信息
 */
void TextChatBubble::setPlainText(const QString &text, QWidget * wrapper)
{
    _text_edit->setPlainText(text);

    // 禁用滚动条
    _text_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _text_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int TEXT_EDIT_MARGIN = 1;
    _text_edit->setContentsMargins(TEXT_EDIT_MARGIN, TEXT_EDIT_MARGIN, TEXT_EDIT_MARGIN, TEXT_EDIT_MARGIN);

    QTextDocument *doc = _text_edit->document();
    doc->setDocumentMargin(2);

    // 气泡最大宽度
    int MAX_BUBBLE_WIDTH = 400;
    doc->setTextWidth(MAX_BUBBLE_WIDTH);

    // 获取设置后，纯文本的的实际尺寸
    qreal text_width = doc->idealWidth();
    qreal text_height = doc->size().height();

    // 得到精确宽高
    int final_edit_width = qCeil(text_width) + TEXT_EDIT_MARGIN * 2;
    int final_edit_height = qCeil(text_height) + TEXT_EDIT_MARGIN * 2;
    // QTextEdit的最终高度
    _text_edit->setFixedSize(final_edit_width, final_edit_height);

    // 外层layout的margins
    auto Margins = _chat_widget->layout()->contentsMargins();
    int margin_width = Margins.left() + Margins.right();
    int margin_height = Margins.top() + Margins.bottom();

    // 外层widget的最终高度
    int final_widget_width = final_edit_width + margin_width;
    int final_widget_height = final_edit_height + margin_height;
    wrapper->setFixedSize(final_widget_width, final_widget_height);
}

QSize TextChatBubble::sizeHint() const
{
    return QSize(width(), height());
}

/**
 * @brief TextChatBubble::initStyleSheet
 * @param wrapper
 * 绘制背景气泡框
 */
void TextChatBubble::initStyleSheet(QWidget * wrapper)
{
    // 文本框本身透明无边框
    _text_edit->setStyleSheet("QTextEdit{background: transparent; border: none}");

    // 根据角色，直接给外壳 wrapper 画上颜色和圆角
    if (_role == ChatRole::Other) {
        wrapper->setStyleSheet("QWidget#text_bubble_wrapper { background-color: white; border-radius: 5px; }");
    } else {
        wrapper->setStyleSheet("QWidget#text_bubble_wrapper { background-color: #9EEA6A; border-radius: 5px; }");
    }
}
