#include "messagereadtracker.h"
#include "messageitemdelegate.h"
#include "messagelistmodel.h"

#include <QApplication>
#include <QEvent>
#include <QListView>
#include <QSet>

QVector<qint64> MessageReadExposure::sample(int chatId, bool active, const QHash<qint64, QRect> &bubbles,
                                           const QRect &viewport, qint64 now)
{
    if (!active || chatId != _chatId) clear();
    _chatId = chatId;
    QVector<qint64> read;
    if (!active || chatId <= 0) return read;
    QSet<qint64> visible;
    for (auto it = bubbles.cbegin(); it != bubbles.cend(); ++it) {
        const auto overlap = viewport.intersected(it.value());
        if (it.key() <= 0 || overlap.isEmpty()
            || overlap.height() * 2 < qMin(viewport.height(), it.value().height())) continue;
        visible.insert(it.key());
        if (!_since.contains(it.key())) _since.insert(it.key(), now);
        if (now - _since.value(it.key()) >= 500) {
            read.push_back(it.key());
            // Keep observing until persistence is visible in the model; retrying an intent is safe.
            _since[it.key()] = now + 4500;
        }
    }
    for (auto it = _since.begin(); it != _since.end();) {
        if (!visible.contains(it.key())) it = _since.erase(it); else ++it;
    }
    return read;
}

MessageReadTracker::MessageReadTracker(QListView *view) : QObject(view), _view(view)
{
    _clock.start();
    qApp->installEventFilter(this);
    _timer.setInterval(50);
    connect(&_timer, &QTimer::timeout, this, &MessageReadTracker::sample);
    _timer.start();
}

bool MessageReadTracker::eventFilter(QObject *watched, QEvent *event)
{
    if (_view && (watched == _view || watched == _view->window() || watched == qApp)
        && (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide
            || event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::WindowStateChange))
        _exposure.clear();
    return QObject::eventFilter(watched, event);
}

void MessageReadTracker::sample()
{
    if (!_view) return;
    const auto *model = qobject_cast<MessageListModel *>(_view->model());
    const auto *delegate = qobject_cast<MessageItemDelegate *>(_view->itemDelegate());
    const bool active = model && delegate && _view->isVisible() && _view->window()->isActiveWindow()
        && !_view->window()->isMinimized() && !QApplication::activeModalWidget();
    QHash<qint64, QRect> bubbles;
    int chatId = 0;
    const auto viewport = _view->viewport()->rect();
    if (active) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto index = model->index(row);
            chatId = index.data(MessageListModel::ChatIdRole).toInt();
            if (index.data(MessageListModel::IsSelfRole).toBool() || !index.data(MessageListModel::DurableRole).toBool()
                || index.data(MessageListModel::ReadConfirmedRole).toBool()) continue;
            const auto rect = _view->visualRect(index);
            if (!rect.intersects(viewport)) continue;
            bubbles.insert(index.data(MessageListModel::MessageIdRole).toLongLong(), delegate->bubbleRect(index, rect));
            if (bubbles.size() >= 256) break;
        }
    }
    const auto read = _exposure.sample(chatId, active, bubbles, viewport, _clock.elapsed());
    if (!read.isEmpty()) emit observed(chatId, read);
}
