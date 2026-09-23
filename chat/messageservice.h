#pragma once

#include "localmessagestore.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <functional>

class MessageStorageWorker;

// Account-scoped coordinator. Network requests/results cross this interface;
// SQLite work stays on one worker, and callbacks are discarded after reset.
/** @brief 在所属 Qt 线程协调账号消息，SQLite 操作排队至单一工作线程；stop 增加代号以丢弃旧账号回调。 */
class MessageService final : public QObject {
    Q_OBJECT
public:
    /** @brief 初始化对象，用于在所属 Qt 线程协调账号消息，SQLite 操作排队至单一工作线程。 */
    explicit MessageService(QObject *parent = nullptr);
    /** @brief 使账号任务失效，排队关闭数据库及退出工作线程，并等待线程结束。 */
    ~MessageService() override;
    /** @brief 结束旧账号代并异步打开新账号存储；uid 或路径无效则保持停用，receipts 控制回执协商能力。 */
    void start(const QString &accountRoot, int uid, bool receipts = false);
    /** @brief 停止定时任务并递增账号代号，丢弃旧账号完成回调；关闭存储按工作队列顺序执行。 */
    void stop();
    /** @brief 查询当前是否关联有效账号，不等同于数据库异步打开成功。 */
    bool isActive() const { return _uid > 0; }
    /** @brief 登记需要同步的会话，首次登记启动消息和回执同步。 */
    void registerChat(int chatId);
    /** @brief 读取已提交游标后请求消息增量；同一会话已有请求时不重复启动。 */
    void synchronize(int chatId);
    /** @brief 验证响应关联、游标及格式后排队落盘，成功才通知模型刷新和后续同步。 */
    void acceptSyncPage(const QJsonObject &response);
    /** @brief 将消息请求排队落盘，持久化完成后再交给发送调度。 */
    void send(const QJsonObject &request);
    /** @brief 将指定 UUID 的服务器 ID 排队合并至当前账号本地存储。 */
    void acknowledge(int chatId, const QString &uuid, qint64 messageId);
    /** @brief 将指定 UUID 的未确认发送状态记为不确定，保留以后对账所需身份。 */
    void markUncertain(int chatId, const QVector<QString> &uuids);
    /** @brief 在存储工作线程查询历史，通过 historyLoaded 返回消息值，旧账号回调被丢弃。 */
    void loadHistory(int chatId, qint64 before = 0, qint64 from = 0);
    /** @brief 将匹配 UUID 与发送批次的 ACK 合并到本地事实，保留幂等身份。 */
    void acceptSendResponse(const QJsonObject &response);
    /** @brief 核对回执请求及 revision 后异步合并，按结果继续同步或重试。 */
    void acceptReceiptResponse(const QJsonObject &response);
    /** @brief 为已登记会话请求独立 revision 回执增量。 */
    void synchronizeReceipts(int chatId);
    /** @brief 保存当前会话已满足可见性条件的消息已读意图，不直接发送网络请求。 */
    void observeRead(int chatId, const QVector<qint64> &ids);
    /** @brief 请求重试指定本地消息，保留客户端 UUID 并由持久化状态决定是否可再次发送。 */
    void retry(int chatId, const QString &uuid);
    /** @brief 暂停自动发送，保留本地消息及批次供以后恢复。 */
    void pauseOutgoing();
signals:
    /** @brief 通知网络层发送已关联当前账号及请求 ID 的消息同步请求。 */
    void syncRequested(QJsonObject request);
    /** @brief 通知网络层发送已持久化并分配 attempt 的消息批次。 */
    void sendRequested(QJsonObject request);
    /** @brief 在服务器 ACK 已应用到本地存储后通知上层。 */
    void sendResponseApplied(QJsonObject response);
    /** @brief 通知网络层发送回执上报或同步请求，id 指定协议消息类型。 */
    void receiptRequested(quint16 id, QJsonObject request);
    /** @brief 通知会话的本地消息事实已变化，需要重新读取模型数据。 */
    void messagesChanged(int chatId);
    /** @brief 通知消息同步页已提交并给出最新已落盘游标。 */
    void synchronized(int chatId, qint64 cursor);
    /** @brief 返回本地历史查询的消息值、原请求游标及后续页标志。 */
    void historyLoaded(int chatId, qint64 before, QVector<StoredMessage> messages, bool hasMore);
    /** @brief 通知指定会话的客户端 UUID 发送失败，供界面展示。 */
    void sendFailed(int chatId, QVector<QString> uuids);
    /** @brief 通知指定会话的存储或同步操作失败，旧账号任务不发出此信号。 */
    void failed(int chatId, QString reason);
private:
    /** @brief 串行读取到期批次，存储完成后发出网络发送请求。 */
    void dispatchOutgoing();
    /** @brief 请求上报指定会话尚未提交的本地回执意图。 */
    void sendReceipts(int chatId);
    /** @brief 根据 report 选择上报或拉取回执，限制并发并登记响应关联。 */
    void requestReceipts(int chatId, bool report);
    /** @brief 安排指定会话回执失败后的有界重试。 */
    void receiptRetry(int chatId);
    /** @brief 将 operation 排入 SQLite 工作线程；完成或失败回到所属线程，仅当前账号代接收回调，回调不可抛异常。 */
    void execute(int chatId, std::function<void(LocalMessageStore &)> operation,
                 std::function<void()> completion = {}, std::function<void()> failure = {});
    int _uid = 0;
    QString _accountRoot;
    QHash<QString, QJsonObject> _failedDrafts;
    quint64 _generation = 0;
    int _pendingOperations = 0;
    QSet<int> _chats;
    QSet<int> _recoveringChats;
    QHash<int, QString> _requests;
    QSet<int> _committing;
    QThread _thread;
    MessageStorageWorker *_worker;
    QTimer _syncTimer;
    QTimer _outgoingTimer;
    bool _dispatching = false;
    bool _receipts = false;
    QHash<QString, QJsonObject> _receiptRequests;
    QSet<int> _receiptSync;
    QSet<int> _receiptReport;
    QSet<QString> _receiptCommitting;
    QHash<int, int> _receiptRetries;
    QSet<int> _receiptSingles;
    QQueue<QPair<int, bool>> _receiptWaiting;
    QSet<qint64> _receiptQueued;
};
