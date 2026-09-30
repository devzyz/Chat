#include "listviewbehavior.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

ListViewBehavior::ListViewBehavior(QAbstractItemView *view)
    : QObject(view), _view(view)
{
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this,
        /** @brief 键盘、拖动及滚轮到达底部时统一安排通知。 */
        [this](int value) {
            if (_view->isVisible() && value == _view->verticalScrollBar()->maximum()) {
                queueBottomCheck();
            }
        });
}

bool ListViewBehavior::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _view) {
        if (event->type() == QEvent::Enter) {
            _view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        } else if (event->type() == QEvent::Leave) {
            _view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }
    if (watched == _view->viewport() && event->type() == QEvent::Wheel) {
        const auto *wheel = static_cast<QWheelEvent *>(event);
        const int delta = wheel->pixelDelta().isNull() ? wheel->angleDelta().y() : wheel->pixelDelta().y();
        if (delta < 0) queueBottomCheck();
    }
    return QObject::eventFilter(watched, event);
}

void ListViewBehavior::queueBottomCheck()
{
    if (_bottomCheckQueued) return;
    _bottomCheckQueued = true;
    QTimer::singleShot(0, this,
        /** @brief 仅在视图仍可见且最终位置到底时发送一次通知。 */
        [this] {
            _bottomCheckQueued = false;
            const auto *bar = _view->verticalScrollBar();
            if (_view->isVisible() && bar->value() == bar->maximum()) emit bottomReached();
        });
}
