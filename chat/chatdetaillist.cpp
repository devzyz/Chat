#include "chatdetaillist.h"
#include <QEvent>
#include <QResizeEvent>
#include <QScrollBar>

ChatDetailList::ChatDetailList(QWidget *parent) : QListView(parent)
{
    setUniformItemSizes(false);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::NoSelection);
    setResizeMode(QListView::Adjust);
    setWordWrap(true);
    setMouseTracking(true);
    viewport()->installEventFilter(this);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value <= 36) {
            emit nearTopReached();
        }
    });
}

bool ChatDetailList::isNearBottom(int tolerance) const
{
    const auto *bar = verticalScrollBar();
    return bar->maximum() - bar->value() <= tolerance;
}

/**
 * @brief ContactUserList::eventFilter
 * @param watched
 * @param event
 * @return
 * 鼠标位于消息区时显示滚动条，移出时隐藏。
 */
bool ChatDetailList::eventFilter(QObject *watched, QEvent *event)
{
    // 如果鼠标进入了这个列表的显示窗口区域
    if (watched == this->viewport()) {
        if (event->type() == QEvent::Enter) {
            // 鼠标悬浮在当前窗口，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }else if(event->type() == QEvent::Leave){
            // 鼠标不在当前窗口，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    return QListView::eventFilter(watched, event);
}

void ChatDetailList::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    emit viewportResized();
}
