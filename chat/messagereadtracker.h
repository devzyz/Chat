#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QVector>

class QListView;

class MessageReadExposure final {
public:
    QVector<qint64> sample(int chatId, bool active, const QHash<qint64, QRect> &bubbles,
                          const QRect &viewport, qint64 now);
    void clear() { _since.clear(); _chatId = 0; }
private:
    int _chatId = 0;
    QHash<qint64, qint64> _since;
};

class MessageReadTracker final : public QObject {
    Q_OBJECT
public:
    explicit MessageReadTracker(QListView *view);
    void resetExposure() { _exposure.clear(); }
signals:
    void observed(int chatId, QVector<qint64> ids);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void sample();
    QPointer<QListView> _view;
    QElapsedTimer _clock;
    QTimer _timer;
    MessageReadExposure _exposure;
};
