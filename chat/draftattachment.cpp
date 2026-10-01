#include "draftattachment.h"
#include <QtConcurrentRun>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QUuid>
#include <QPromise>
#include <atomic>

namespace {
std::atomic<int> pendingAttachments{0};
std::atomic<qint64> pendingImageBytes{0};
constexpr int maxPendingAttachments = 32;
constexpr qint64 maxPendingImageBytes = 256 * 1024 * 1024;

/** @brief 一次后台准备占用的队列与图片内存额度，任务退出时归还。 */
struct PreparationReservation {
    qint64 bytes = 0;
    /** @brief 释放任务数量及其捕获图片的内存额度。 */
    ~PreparationReservation() {
        pendingImageBytes.fetch_sub(bytes);
        pendingAttachments.fetch_sub(1);
    }
};
}

std::shared_ptr<DraftAttachment> prepareDraftAttachment(const QString &path, const QImage &image,
                                                       const QString &accountRoot)
{
    auto attachment = std::make_shared<DraftAttachment>();
    attachment->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto bytes = static_cast<qint64>(image.sizeInBytes());
    const auto count = pendingAttachments.fetch_add(1);
    const auto priorBytes = pendingImageBytes.fetch_add(bytes);
    auto reservation = std::make_shared<PreparationReservation>();
    reservation->bytes = bytes;
    if (count >= maxPendingAttachments || bytes > maxPendingImageBytes
        || priorBytes > maxPendingImageBytes - bytes) {
        QPromise<DraftAttachmentResult> rejected;
        rejected.start();
        DraftAttachmentResult result;
        result.error = QObject::tr("附件准备队列已满（最多 32 项、图片内存 256 MiB），请删除此项后稍后重试");
        rejected.addResult(result); rejected.finish(); attachment->future = rejected.future();
        return attachment;
    }
    auto *ownerThread = QCoreApplication::instance()->thread();
    attachment->future = QtConcurrent::run(
        /** @brief 仅处理值参数；文件关闭后交给主线程中的共享草稿引用。 */
        [path, image, accountRoot, id = attachment->id, ownerThread, reservation,
         reference = std::weak_ptr<DraftAttachment>(attachment)] {
        DraftAttachmentResult result;
        if (reference.expired()) return result;
        result.entry = {DraftEntry::Kind::Attachment, id, path, {}};
        if (!image.isNull()) {
            const auto directory = accountRoot + "/transfers/drafts";
            if (accountRoot.isEmpty() || !QDir().mkpath(directory)) {
                result.error = QObject::tr("无法创建剪贴板图片目录"); return result;
            }
            auto file = std::make_shared<QTemporaryFile>(directory + "/clipboard-XXXXXX.png");
            if (!file->open() || !image.save(file.get(), "PNG")) {
                result.error = QObject::tr("剪贴板图片保存失败"); return result;
            }
            file->close(); file->moveToThread(ownerThread);
            result.entry.content = file->fileName(); result.entry.temporary = file;
        }
        if (reference.expired()) return DraftAttachmentResult{};
        const QFileInfo file(result.entry.content);
        if (!file.isFile() || !file.isReadable() || file.size() == 0) {
            result.error = QObject::tr("附件不存在、为空或不可读，请删除或重新添加"); return result;
        }
        result.entry.content = file.absoluteFilePath();
        QImageReader reader(result.entry.content);
        if (reference.expired()) return DraftAttachmentResult{};
        if (reader.size().isValid()) reader.setScaledSize(reader.size().scaled(120, 80, Qt::KeepAspectRatio));
        result.preview = reader.read();
        if (result.preview.isNull()) {
            result.preview = QImage(48, 48, QImage::Format_RGB32); result.preview.fill(Qt::lightGray);
        }
        return result;
    });
    return attachment;
}
