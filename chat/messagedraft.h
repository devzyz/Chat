#pragma once
#include <QString>
#include <QVector>
#include <QTemporaryFile>
#include <memory>
/** @brief 草稿条目；临时文件共享所有权覆盖编辑器撤销记录及发送任务。 */
struct DraftEntry {
    enum class Kind { Text, Attachment };
    Kind kind = Kind::Text;
    QString id;
    QString content;
    std::shared_ptr<QTemporaryFile> temporary;
};
/** @brief 当前编辑版本的提交快照，读取不消费原文档。 */
struct MessageDraft {
    QString id;
    QVector<DraftEntry> entries;
    QString error;
};
