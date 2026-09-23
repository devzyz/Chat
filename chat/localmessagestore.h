#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include "messagestate.h"
#include <QLockFile>
#include <QMetaType>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <QSet>
#include <memory>

// Persistence values contain no widgets, pixmaps or presentation text.
/** @brief 保存可持久化消息事实，不包含 QWidget、QPixmap 或展示文案。 */
struct StoredMessage {
    enum State { Pending, Confirmed, Uncertain, Failed, Queued };
    qint64 localId = 0;
    qint64 messageId = 0;
    QString clientMessageId;
    int chatId = 0;
    int senderId = 0;
    int recipientId = 0;
    QString content;
    qint64 sentAt = 0;
    State state = Pending;
    ReceiptLevel receipt = ReceiptLevel::None;
};
Q_DECLARE_METATYPE(StoredMessage)
Q_DECLARE_METATYPE(QVector<StoredMessage>)

/** @brief 保存本地历史页的消息值及是否仍有较早记录。 */
struct LocalMessagePage {
    QVector<StoredMessage> messages;
    bool hasMore = false;
};

// All methods, including destruction, run on the connection's owning thread.
// Errors throw; a failed transaction never advances a synchronization cursor.
/** @brief 独占账号 SQLite 连接及锁文件；所有方法和析构均在连接所属工作线程执行，存储错误抛异常。 */
class LocalMessageStore final {
public:
    /** @brief 默认构造空状态，实际账号或资源在显式初始化时接入。 */
    LocalMessageStore() = default;
    /** @brief 关闭账号数据库并移除本对象的 SQLite 连接。 */
    ~LocalMessageStore();
    /** @brief 禁止复制该对象，避免共享可变状态或重复管理资源。 */
    LocalMessageStore(const LocalMessageStore &) = delete;
    /** @brief 禁止复制赋值，避免混淆独占资源和连接所有权。 */
    LocalMessageStore &operator=(const LocalMessageStore &) = delete;
    /** @brief 在工作线程打开账号数据库并获得独占锁，校验或升级 schema；失败抛异常。 */
    void open(const QString &accountRoot, int uid = 0);
    /** @brief 在工作线程关闭数据库并释放连接及账号锁，可重复调用。 */
    void close();
    /** @brief 查询当前工作线程持有的 SQLite 连接是否打开。 */
    bool isOpen() const { return _db.isOpen(); }
    /** @brief 读取指定会话已经事务提交的消息同步游标。 */
    qint64 cursor(int chatId);
    /** @brief 在同一事务中合并服务器消息并推进匹配 previous 的游标；失败不推进游标。 */
    void applySyncPage(int chatId, qint64 previous, qint64 next, const QVector<StoredMessage> &messages);
    /** @brief 将待发消息落入本地存储，保留客户端 UUID 以支持重试。 */
    void saveOutgoing(const QVector<StoredMessage> &messages);
    /** @brief 按会话、发送者和 UUID 将已发送消息与服务器 ID 关联，不推断送达或已读。 */
    void acknowledge(int chatId, int senderId, const QString &uuid, qint64 messageId);
    /** @brief 将指定 UUID 的未确认发送状态记为不确定，保留以后对账所需身份。 */
    void markUncertain(int chatId, const QVector<QString> &uuids);
    /** @brief 按 before 读取至多 limit 条历史记录，from 限定范围；返回消息副本和后续页标志。 */
    LocalMessagePage history(int chatId, qint64 before, int limit, qint64 from = 0);
    /** @brief 将发送批次及其消息事务落盘，供断线重启后继续调度。 */
    void saveOutgoingRequest(const QJsonObject &request);
    /** @brief 按毫秒时间 now 选择到期发送批次并持久化 attempt；恢复中的会话暂缓，changed 可返回受影响会话。 */
    QVector<QJsonObject> dispatchDue(qint64 now, const QSet<int> &recovering = {}, QSet<int> *changed = nullptr);
    /** @brief 返回仍有未完成发送批次、需要先与服务器对账的会话集合。 */
    QSet<int> recoveryChats();
    /** @brief 将匹配 UUID 与发送批次的 ACK 合并到本地事实，保留幂等身份。 */
    void acceptSendResponse(const QJsonObject &response);
    /** @brief 请求重试指定本地消息，保留客户端 UUID 并由持久化状态决定是否可再次发送。 */
    void retry(const QString &uuid);
    /** @brief 暂停自动发送，保留本地消息及批次供以后恢复。 */
    void pauseOutgoing();
    /** @brief 恢复符合条件的本地发送批次，供后续到期调度使用。 */
    void resumeOutgoing();
    /** @brief 读取指定会话的回执 revision 游标。 */
    qint64 receiptCursor(int chatId);
    /** @brief 返回指定会话尚待上报的送达及已读意图。 */
    QJsonArray pendingReceipts(int chatId);
    /** @brief 保存当前会话已满足可见性条件的消息已读意图，不直接发送网络请求。 */
    void observeRead(int chatId, const QVector<qint64> &ids);
    /** @brief 事务合并回执事实，指定 previous/next 时同时校验和推进 revision 游标。 */
    void acceptReceipts(int chatId, const QJsonArray &items, qint64 previous = -1, qint64 next = -1);
    /** @brief 删除指定会话消息已无须继续上报的回执意图。 */
    void discardReceipt(int chatId, qint64 messageId);
private:
    /** @brief 在现有事务范围保存待发消息行，供批次与消息原子写入复用。 */
    void saveOutgoingRows(const QVector<StoredMessage> &messages);
    /** @brief 为本地已经落盘的接收消息生成送达意图。 */
    void queueDelivered(int chatId);
    /** @brief 清理消息已经确认的发送批次，避免重复调度。 */
    void clearCommittedBatches();
    /** @brief 按服务器 ID 与客户端 UUID 合并持久化消息，冲突时抛异常。 */
    void mergeServerMessage(const StoredMessage &message);
    int _uid = 0;
    QSqlDatabase _db;
    QString _connection;
    std::unique_ptr<QLockFile> _lock;
};
