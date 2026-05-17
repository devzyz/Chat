#include "textbubble.h"
#include <QEvent>
#include <QTextBlock>
#include <QFontMetricsF>

TextBubble::TextBubble(ChatRole role, const QString &text, QWidget *parent) :
    BubbleFrame(role, parent)
{
    // 创建一个QTextEdit用来显示聊天信息
    m_pTextEdit = new QTextEdit();
    m_pTextEdit->setReadOnly(true); // 设置为只读
    // 关闭滚动条
    m_pTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pTextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 给他安装一个eventFilter事件
    m_pTextEdit->installEventFilter(this);
    // 设置字体
    QFont font("Microsoft YaHei");
    font.setPixelSize(12);
    m_pTextEdit->setFont(font);

    setPlainText(text);
    // 将这个文本框，放到父节点的布局内
    setWidget(m_pTextEdit);
    initStyleSheet();
}

bool TextBubble::eventFilter(QObject *o, QEvent *e) {
    // 绘制事件发生之前，先做一下这个事件
    if (m_pTextEdit == o && e->type() == QEvent::Paint) {
        adjustTextHeight();
    }
    return BubbleFrame::eventFilter(o, e);
}

QSize TextBubble::sizeHint() const
{
    return QSize(this->width(), this->height());
}

void TextBubble::adjustTextHeight() {
    // 获取文本和m_pTextEdit之间的边距
    qreal doc_margin = m_pTextEdit->document()->documentMargin();
    QTextDocument * doc = m_pTextEdit->document();
    qreal text_height = 0;
    // 把每一行的高度相加
    for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next()) {
        QTextLayout * pLayout = it.layout();
        QRectF text_rect = pLayout->boundingRect();
        text_height += text_rect.height();
    }

    int vMargin = this->layout()->contentsMargins().top();
    // 设置整个气泡的高度 文本高度 + 文本与m_pTextEdit之间的高度 * 2 + m_pTextEdit与外边框之间的高度 * 2
    setFixedHeight(text_height + doc_margin * 2 + vMargin * 2);
}

void TextBubble::setPlainText(const QString &text) {
    // 设置文本
    m_pTextEdit->setPlainText(text);
    //找到段落中最大宽度
    qreal doc_margin = m_pTextEdit->document()->documentMargin();
    // 气泡布局的左右边距
    int margin_left = this->layout()->contentsMargins().left();
    int margin_right = this->layout()->contentsMargins().right();
    QFontMetricsF fm(m_pTextEdit->font());
    QTextDocument *doc = m_pTextEdit->document();
    int max_width = 0;
    //遍历每一段找到 最宽的那一段
    for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next())    //字体总长
    {
        int txtW = int(fm.horizontalAdvance(it.text()));
        max_width = max_width < txtW ? txtW : max_width;                 //找到最长的那段
    }
    //设置这个气泡的最大宽度 只需要设置一次
    setMaximumWidth(max_width + doc_margin * 2 + (margin_left + margin_right) + 1);        //设置最大宽度
}

void TextBubble::initStyleSheet() {
    m_pTextEdit->setStyleSheet("QTextEdit{background : transparent; border : none}");
}
