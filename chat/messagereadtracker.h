#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QVector>

class QListView;

/** @brief 记录当前会话气泡的连续可见时长，窗口不活跃或可见条件中断时重新计时。 */
class MessageReadExposure final {
public:
    /** @brief 以单调毫秒时间采样气泡与视口，返回连续可见至少 500ms 的消息 ID；会话或活跃状态变化清除旧计时。 */
    QVector<qint64> sample(int chatId, bool active, const QHash<qint64, QRect> &bubbles,
                          const QRect &viewport, qint64 now);
    /** @brief 清除连续可见计时和当前会话标识。 */
    void clear() { _since.clear(); _chatId = 0; }
private:
    int _chatId = 0;
    QHash<qint64, qint64> _since;
};

/** @brief 通过视图事件和定时采样生成已读意图；弱引用视图并在 GUI 线程运行。 */
class MessageReadTracker final : public QObject {
    Q_OBJECT
public:
    /** @brief 初始化对象，用于通过视图事件和定时采样生成已读意图。 */
    explicit MessageReadTracker(QListView *view);
    /** @brief 清除当前可见累计，后续采样重新满足连续可见期限。 */
    void resetExposure() { _exposure.clear(); }
signals:
    /** @brief 通知当前前台会话中持续可见满足期限的消息 ID，供持久化层生成已读意图。 */
    void observed(int chatId, QVector<qint64> ids);
protected:
    /** @brief 观察关联对象事件并处理本控件负责的交互，其余事件交回 Qt。 */
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    /** @brief 仅对活动窗口当前会话采样真实气泡矩形，满足持续可见条件后发出 observed。 */
    void sample();
    QPointer<QListView> _view;
    QElapsedTimer _clock;
    QTimer _timer;
    MessageReadExposure _exposure;
};
