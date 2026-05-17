#include "chatview.h"
#include <QScrollBar>
#include <QStyleOption>
#include <QPainter>

ChatView::ChatView(QWidget *parent) : QWidget(parent), isAppended(false)
{
    // 对当前widget窗口，设置为垂直布局，并规定外边距为0
    QVBoxLayout *pMainLayout = new QVBoxLayout();
    this->setLayout(pMainLayout);
    pMainLayout->setContentsMargins(0, 0, 0, 0);

    // 给垂直布局内部，添加一个滚动区域，并命名为chat_scrollarea
    m_pScrollArea= new QScrollArea();
    m_pScrollArea->setObjectName("chat_scrollarea");
    pMainLayout->addWidget(m_pScrollArea);

    // 用于显示消息的widget
    QWidget *w = new QWidget(this);
    w->setObjectName("chat_background");
    w->setAutoFillBackground(true);

    // 为widget创建了一个垂直布局，并为垂直布局添加了一个widget,他的拉伸因子很大，会尽可能的占用所有的多余空间
    QVBoxLayout *pHLayout_1 = new QVBoxLayout();
    pHLayout_1->addWidget(new QWidget(), 100000);
    w->setLayout(pHLayout_1);
    // 将w设置为滚动区域的内部组件
    m_pScrollArea->setWidget(w);

    // 关闭滚动条
    m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 获取到滚动区域的滚动条
    QScrollBar * pVScrollBar = m_pScrollArea->verticalScrollBar();

    // 绑定滚动条信号与槽，当内容高度发生变化时，触发槽函数
    connect(pVScrollBar, &QScrollBar::rangeChanged, this, &ChatView::onVScrollBarMoved);

    // 把垂直ScrollBar放到上面
    // 创建一个水平布局
    QHBoxLayout * pHLayout_2 = new QHBoxLayout();
    pHLayout_2->addWidget(pVScrollBar, 0, Qt::AlignRight);
    pHLayout_2->setContentsMargins(0, 0, 0, 0);
    m_pScrollArea->setLayout(pHLayout_2);
    pVScrollBar->setHidden(true);

    m_pScrollArea->setWidgetResizable(true);
    m_pScrollArea->installEventFilter(this);
}

void ChatView::appendChatItem(QWidget *item)
{
    QVBoxLayout *vl = qobject_cast<QVBoxLayout *> (m_pScrollArea->widget()->layout());
    vl->insertWidget(vl->count() - 1, item);
    isAppended = true;
}

void ChatView::prependChatItem(QWidget *item)
{

}

void ChatView::insertChatItem(QWidget *before, QWidget *item)
{

}

bool ChatView::eventFilter(QObject *o, QEvent *e)
{
    // 如果是进入事件，同时进入的对象是滚动区域
    if (e->type() == QEvent::Enter && o == m_pScrollArea) {
        // 滚动条最大值为0，说明当前内容没有超过显示区域，则隐藏滚动条
        m_pScrollArea->verticalScrollBar()->setHidden(m_pScrollArea->verticalScrollBar()->maximum() == 0);
    }else if (e->type() == QEvent::Leave && o == m_pScrollArea) {
        m_pScrollArea->verticalScrollBar()->setHidden(true);
    }
    return QWidget::eventFilter(o, e);
}

/**
 * @brief ChatView::paintEvent
 * @param event
 * 为了给自定义的widget使用qss样式
 */
void ChatView::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

/**
 * @brief ChatView::onVScrollBarMoved
 * @param min
 * @param max
 * 消息内容变化后，将滚动条设置到最大位置，即新消息自动可见
 */
void ChatView::onVScrollBarMoved(int min, int max)
{
    if (isAppended) {
        QScrollBar * pVScrollBar = m_pScrollArea->verticalScrollBar();
        pVScrollBar->setSliderPosition(pVScrollBar->maximum());
        // 500 毫秒内可再次调用
        QTimer::singleShot(500, [this]() {
            isAppended = false;
        });
    }
}
