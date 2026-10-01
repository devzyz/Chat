#pragma once
#include "messagedraft.h"
#include <QTextEdit>
#include <QHash>
#include <QMap>
#include <QSet>
#include "draftattachment.h"
#include <QTextDocument>
#include <QTextCursor>
/** @brief 编辑文本及稳定身份附件；文档快照无副作用，临时文件覆盖撤销生命周期。 */
class MessageTextEdit : public QTextEdit {
    Q_OBJECT
public:
    /** @brief 创建支持文本、附件及发送快捷键的编辑器。 */
    explicit MessageTextEdit(QWidget *parent = nullptr);
    /** @brief 切换会话的原生编辑文档，保留撤销、光标和附件所有权。 */
    void setConversation(int chatId);
    /** @brief 检查全部会话是否存在未提交正文或附件占位。 */
    bool hasDrafts() const;
    /** @brief 读取当前文档顺序的草稿，不清空输入。 */
    MessageDraft draft() const;
    /** @brief 清除已被接受且仍为当前版本的草稿和撤销记录。 */
    void clearAccepted(const QString &id);
    /** @brief 设置当前账号可写目录，用于暂存剪贴板图片。 */
    void setAccountRoot(const QString &root);
    /** @brief 插入附件占位并后台校验；空路径同步拒绝，准备失败保留占位并发出说明。 */
    bool addAttachment(const QString &path);
    /** @brief 当前文档是否仍有后台准备中的附件，准备完成前提交保留原文。 */
    bool hasPendingAttachments() const;
signals:
    /** @brief 用户请求提交当前草稿，尚不代表落盘。 */
    void send();
    /** @brief 输入不能接收时返回可展示原因。 */
    void inputRejected(QString reason);
protected:
    /** @brief 为内部剪贴板保存附件类型及临时文件引用。 */
    QMimeData *createMimeDataFromSelection() const override;
    /** @brief 接受文本、图片及本地文件 MIME。 */
    bool canInsertFromMimeData(const QMimeData *source) const override;
    /** @brief 插入 MIME 内容，普通文本交给 Qt 保持编辑语义。 */
    void insertFromMimeData(const QMimeData *source) override;
    /** @brief Enter 请求发送，Shift+Enter 保留换行。 */
    void keyPressEvent(QKeyEvent *event) override;
    /** @brief 外部拖入按 MIME 能力决定是否接收。 */
    void dragEnterEvent(QDragEnterEvent *event) override;
    /** @brief 插入外部拖放内容，内部移动由 Qt 处理。 */
    void dropEvent(QDropEvent *event) override;
private:
    /** @brief 原生文档由编辑器的 QObject 树拥有，状态按会话保留附件与撤销引用。 */
    struct DraftState {
        QTextDocument *document = nullptr;
        QTextCursor cursor;
        QHash<QString, std::shared_ptr<DraftAttachment>> attachments;
        QMap<int, QSet<QString>> history;
        QString id;
    };
    /** @brief 只保留当前文档和可达撤销分支引用的附件。 */
    void reconcileAttachments();
    /** @brief 用稳定资源 ID 在当前位置插入附件占位图。 */
    void insertAttachment(const std::shared_ptr<DraftAttachment> &attachment);
    /** @brief 将已完成预览绑定到文档，或者监听后台任务完成；不改变撤销栈。 */
    void observeAttachment(const std::shared_ptr<DraftAttachment> &attachment);
    QHash<int, std::shared_ptr<DraftState>> _drafts;
    std::shared_ptr<DraftState> _active;
    bool _switching = false;
    QString _accountRoot;
};
