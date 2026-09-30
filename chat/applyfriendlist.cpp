#include "applyfriendlist.h"
#include "listviewbehavior.h"
#include <QEvent>

ApplyFriendList::ApplyFriendList(QWidget *parent) : QListWidget(parent)
{
    new ListViewBehavior(this);
    viewport()->installEventFilter(this);
}

bool ApplyFriendList::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && event->type() == QEvent::MouseButtonPress) {
        emit searchRequested(false);
    }
    return QListWidget::eventFilter(watched, event);
}
