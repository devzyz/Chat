#include "chatdetaillist.h"
#include "listviewbehavior.h"
#include <QEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include "messagelistmodel.h"

ChatDetailList::ChatDetailList(QWidget *parent) : QListView(parent)
{
    setUniformItemSizes(false);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    new ListViewBehavior(this);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
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

void ChatDetailList::copyCurrentMessage()
{
    if (!currentIndex().isValid()) return;
    auto text = currentIndex().data(MessageListModel::TextRole).toString();
    if (!currentIndex().data(MessageListModel::ResourceIdRole).toString().isEmpty()) {
        const auto suffix = tr("（双击打开或重试下载）");
        if (text.endsWith(suffix)) text.chop(suffix.size());
    }
    QApplication::clipboard()->setText(text);
}

void ChatDetailList::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) { copyCurrentMessage(); event->accept(); return; }
    QListView::keyPressEvent(event);
}

void ChatDetailList::contextMenuEvent(QContextMenuEvent *event)
{
    const auto index = event->reason() == QContextMenuEvent::Keyboard ? currentIndex() : indexAt(event->pos());
    if (!index.isValid()) return;
    setCurrentIndex(index);
    QMenu menu(this);
    menu.addAction(tr("复制"), this, &ChatDetailList::copyCurrentMessage);
    menu.exec(event->globalPos());
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
