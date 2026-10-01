#include "messagetextedit.h"
#include <QMimeData>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QFileInfo>
#include <QDir>
#include <QImageReader>
#include <QUuid>
#include <QTextDocumentFragment>
#include <QFutureWatcher>

/** @brief 进程内剪贴板保存原始片段和附件，避免把对象占位符当成文本发送。 */
class DraftMimeData final : public QMimeData {
public:
    QTextDocumentFragment fragment;
    QHash<QString, std::shared_ptr<DraftAttachment>> attachments;
    QString accountRoot;
};
MessageTextEdit::MessageTextEdit(QWidget *parent) : QTextEdit(parent)
{
    setMaximumHeight(100);
    setAcceptRichText(false);
    _draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    connect(this, &QTextEdit::textChanged, this, /** @brief 每次编辑产生新提交身份。 */ [this] {
        _draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        reconcileAttachments();
    });
}
QMimeData *MessageTextEdit::createMimeDataFromSelection() const
{
    auto *mime = new DraftMimeData;
    std::unique_ptr<QMimeData> standard(QTextEdit::createMimeDataFromSelection());
    for (const auto &format : standard->formats()) mime->setData(format, standard->data(format));
    mime->fragment = QTextDocumentFragment(textCursor());
    mime->accountRoot = _accountRoot;
    QTextDocument selected;
    QTextCursor(&selected).insertFragment(mime->fragment);
    for (auto block = selected.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto id = it.fragment().charFormat().toImageFormat().name();
            if (_attachments.contains(id)) mime->attachments.insert(id, _attachments.value(id));
        }
    }
    return mime;
}
void MessageTextEdit::reconcileAttachments()
{
    const int step = document()->availableUndoSteps();
    if (!document()->isRedoAvailable()) {
        auto it = _attachmentHistory.upperBound(step);
        while (it != _attachmentHistory.end()) it = _attachmentHistory.erase(it);
    }
    QSet<QString> current;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto format = it.fragment().charFormat();
            if (format.isImageFormat()) current.insert(format.toImageFormat().name());
        }
    }
    _attachmentHistory[step] = current;
    QSet<QString> reachable;
    for (const auto &ids : _attachmentHistory) reachable.unite(ids);
    for (auto it = _attachments.begin(); it != _attachments.end();) {
        if (!reachable.contains(it.key())) it = _attachments.erase(it);
        else ++it;
    }
}
MessageDraft MessageTextEdit::draft() const
{
    MessageDraft result{_draftId, {}};
    QString text;
    const auto flush = /** @brief 将相邻文本合为一项并保留换行。 */ [&] {
        if (!text.isEmpty()) result.entries.push_back({DraftEntry::Kind::Text, {}, text, {}});
        text.clear();
    };
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (block != document()->begin()) text += '\n';
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const auto format = fragment.charFormat();
            if (format.isImageFormat()) {
                flush();
                const auto id = format.toImageFormat().name();
                const auto attachment = _attachments.value(id);
                if (!attachment) result.error = tr("附件身份无法恢复，请删除后重新添加");
                else if (!attachment->future.isFinished()) result.error = tr("附件正在准备，请稍后发送");
                else {
                    const auto prepared = attachment->future.result();
                    if (!prepared.error.isEmpty()) result.error = prepared.error;
                    else for (qsizetype i = 0; i < fragment.length(); ++i) result.entries.push_back(prepared.entry);
                }
            } else text += fragment.text();
        }
    }
    flush();
    return result;
}
void MessageTextEdit::clearAccepted(const QString &id)
{
    if (id != _draftId) return;
    clear();
    document()->clearUndoRedoStacks();
    _attachments.clear();
    _attachmentHistory.clear();
}
void MessageTextEdit::setAccountRoot(const QString &root) { _accountRoot = root; }
bool MessageTextEdit::addAttachment(const QString &path)
{
    if (path.isEmpty()) { emit inputRejected(tr("附件路径为空")); return false; }
    insertAttachment(prepareDraftAttachment(path, {}, _accountRoot));
    return true;
}
bool MessageTextEdit::hasPendingAttachments() const
{
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto attachment = _attachments.value(it.fragment().charFormat().toImageFormat().name());
            if (attachment && !attachment->future.isFinished()) return true;
        }
    }
    return false;
}
void MessageTextEdit::observeAttachment(const std::shared_ptr<DraftAttachment> &attachment)
{
    auto *watcher = new QFutureWatcher<DraftAttachmentResult>(this);
    connect(watcher, &QFutureWatcher<DraftAttachmentResult>::finished, this,
        /** @brief 后台完成只刷新仍被该文档持有的预览，不改变位置或撤销栈。 */
        [this, watcher, id = attachment->id] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!_attachments.contains(id)) return;
        if (!result.error.isEmpty()) emit inputRejected(result.error);
        else {
            document()->addResource(QTextDocument::ImageResource, QUrl(id), result.preview);
            document()->markContentsDirty(0, document()->characterCount());
            viewport()->update();
        }
        _draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    });
    watcher->setFuture(attachment->future);
}
void MessageTextEdit::insertAttachment(const std::shared_ptr<DraftAttachment> &attachment)
{
    QImage preview(120, 80, QImage::Format_RGB32); preview.fill(Qt::lightGray);
    document()->addResource(QTextDocument::ImageResource, QUrl(attachment->id), preview);
    QTextImageFormat format;
    format.setName(attachment->id);
    format.setWidth(preview.width()); format.setHeight(preview.height());
    format.setToolTip(tr("附件：准备完成后可发送；准备失败请删除后重新添加"));
    _attachments.insert(attachment->id, attachment);
    auto cursor = textCursor(); cursor.insertImage(format); setTextCursor(cursor);
    observeAttachment(attachment);
}
bool MessageTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls() || QTextEdit::canInsertFromMimeData(source);
}
void MessageTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (const auto *internal = dynamic_cast<const DraftMimeData *>(source)) {
        if (!internal->attachments.isEmpty() && internal->accountRoot != _accountRoot) {
            emit inputRejected(tr("不能粘贴其他账号的附件")); return;
        }
        for (auto it = internal->attachments.begin(); it != internal->attachments.end(); ++it) {
            _attachments.insert(it.key(), it.value());
            observeAttachment(it.value());
        }
        auto cursor = textCursor(); cursor.insertFragment(internal->fragment); setTextCursor(cursor);
        return;
    }
    if (source->hasUrls()) {
        for (const auto &url : source->urls()) {
            if (url.isLocalFile()) addAttachment(url.toLocalFile());
            else emit inputRejected(tr("仅支持拖入本地文件"));
        }
        return;
    }
    if (source->hasImage()) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (_accountRoot.isEmpty() || image.isNull()) {
            emit inputRejected(tr("无法保存剪贴板图片，请检查账号目录")); return;
        }
        insertAttachment(prepareDraftAttachment({}, image, _accountRoot));
        return;
    }
    if (source->hasText()) QTextEdit::insertFromMimeData(source);
    else emit inputRejected(tr("不支持此剪贴板内容"));
}
void MessageTextEdit::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & Qt::ShiftModifier)) { emit send(); return; }
    QTextEdit::keyPressEvent(event);
}
void MessageTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (canInsertFromMimeData(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}
void MessageTextEdit::dropEvent(QDropEvent *event)
{
    if (event->source() == this) { QTextEdit::dropEvent(event); return; }
    setTextCursor(cursorForPosition(event->position().toPoint()));
    insertFromMimeData(event->mimeData()); event->acceptProposedAction();
}
