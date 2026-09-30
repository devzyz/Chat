#include "chatdetaillist.h"
#include "listviewbehavior.h"
#include <QEvent>
#include <QResizeEvent>
#include <QScrollBar>

ChatDetailList::ChatDetailList(QWidget *parent) : QListView(parent)
{
    setUniformItemSizes(false);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    new ListViewBehavior(this);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::NoSelection);
    setResizeMode(QListView::Adjust);
    setWordWrap(true);
    setMouseTracking(true);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
        /** @brief 滚动接近顶部时通知页面加载历史。 */
        [this](int value) {
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

void ChatDetailList::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    emit viewportResized();
}
