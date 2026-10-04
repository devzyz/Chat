#pragma once
#include "messagedraft.h"
#include <QFuture>
#include <QImage>

/** @brief 后台附件准备结果；临时文件引用随后由文档、剪贴板或提交任务持有。 */
struct DraftAttachmentResult {
    DraftEntry entry;
    QImage preview;
    qint64 byteSize = 0;
    QString error;
};

/** @brief 不可变附件身份与异步结果，不依赖编辑器生命周期。 */
struct DraftAttachment {
    QString id;
    QString displayName;
    QFuture<DraftAttachmentResult> future;
};

/** @brief 在线程池准备附件，最多 32 项/256 MiB 图片内存；满载返回失败结果，无引用任务跳过后续步骤。 */
std::shared_ptr<DraftAttachment> prepareDraftAttachment(const QString &path, const QImage &image,
                                                       const QString &accountRoot);
