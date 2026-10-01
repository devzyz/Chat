#pragma once
#include "messagedraft.h"
#include <QObject>
#include <QJsonObject>
class MessageService;
class ResourceTransferManager;
/** @brief 固定一次提交的账号、会话和群代次；串行上传并将消息所有权移交 outbox。 */
class MessageSubmissionController final : public QObject {
    Q_OBJECT
public:
    /** @brief 借用同账号服务及专用上传器，其生命周期必须覆盖本对象。 */
    MessageSubmissionController(MessageService *messages, ResourceTransferManager *uploads,
                                int uid, QObject *parent = nullptr);
    /** @brief 接受草稿快照；忙碌、重复、空输入或无权限返回 false，原编辑器应保留内容。 */
    bool submit(const MessageDraft &draft, int chatId, int recipient, bool group);
    /** @brief 原身份重试失败条目，不重发已落盘部分。 */
    void retry();
    /** @brief 取消未落盘部分；已排队的存储事务完成后才释放任务。 */
    void cancel();
    /** @brief 是否仍拥有未移交消息服务的内容。 */
    bool hasPending() const { return !_entries.isEmpty(); }
    /** @brief 是否处于可重试失败状态。 */
    bool isFailed() const { return _failed; }
    /** @brief 返回当前进度或失败提示，供页面重建恢复展示。 */
    QString status() const { return _status; }
    /** @brief 返回未落盘内容摘要，供界面在清空编辑器后仍可查看任务。 */
    QString pendingSummary() const;
    /** @brief 将文本按 JSON 字节预算拆分为稳定 UUID 请求，不切断代理对。 */
    static QVector<QJsonObject> textRequests(const QString &text, const QJsonObject &context);
signals:
    /** @brief 进度或失败状态改变，界面据此呈现重试和取消入口。 */
    void stateChanged();
    /** @brief 草稿不能接收，原输入仍归编辑器所有。 */
    void rejected(QString reason);
private:
    /** @brief 推进当前草稿条目，等待上传或明确落盘结果。 */
    void advance();
    /** @brief 发送当前请求并等待 UUID 对应的落盘通知。 */
    void persist();
    /** @brief 记录可恢复失败，保留当前内容及 UUID。 */
    void fail(const QString &reason);
    /** @brief 释放本次任务引用，不删除已有 outbox 数据。 */
    void finish();
    MessageService *_messages;
    ResourceTransferManager *_uploads;
    int _uid;
    QString _lastDraft, _status, _uploadUuid;
    QVector<DraftEntry> _entries;
    QVector<QJsonObject> _requests;
    QJsonObject _context;
    bool _failed = false, _saving = false, _uploading = false, _cancelAfterSave = false;
};
