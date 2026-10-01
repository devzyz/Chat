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
#include <QSignalBlocker>

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
    setConversation(0);
    connect(this, &QTextEdit::textChanged, this, /** @brief 每次编辑产生新提交身份。 */ [this] {
        if (_switching) return;
        _active->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
            if (_active->attachments.contains(id)) mime->attachments.insert(id, _active->attachments.value(id));
        }
    }
    return mime;
}
void MessageTextEdit::reconcileAttachments()
{
    const int step = document()->availableUndoSteps();
    if (!document()->isRedoAvailable()) {
        auto it = _active->history.upperBound(step);
        while (it != _active->history.end()) it = _active->history.erase(it);
    }
    QSet<QString> current;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto format = it.fragment().charFormat();
            if (format.isImageFormat()) current.insert(format.toImageFormat().name());
        }
    }
    _active->history[step] = current;
    QSet<QString> reachable;
    for (const auto &ids : _active->history) reachable.unite(ids);
    for (auto it = _active->attachments.begin(); it != _active->attachments.end();) {
        if (!reachable.contains(it.key())) it = _active->attachments.erase(it);
        else ++it;
    }
}
MessageDraft MessageTextEdit::draft() const
{
    MessageDraft result{_active->id, {}};
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
                const auto attachment = _active->attachments.value(id);
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
    if (id != _active->id) return;
    clear();
    document()->clearUndoRedoStacks();
    _active->attachments.clear();
    _active->history.clear();
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
            const auto attachment = _active->attachments.value(it.fragment().charFormat().toImageFormat().name());
            if (attachment && !attachment->future.isFinished()) return true;
        }
    }
    return false;
}
void MessageTextEdit::observeAttachment(const std::shared_ptr<DraftAttachment> &attachment)
{
    auto *watcher = new QFutureWatcher<DraftAttachmentResult>(this);
    const std::weak_ptr<DraftState> state = _active;
    connect(watcher, &QFutureWatcher<DraftAttachmentResult>::finished, this,
        /** @brief 后台结果只写入原草稿，已清除或已释放的附件不再刷新。 */
        [this, watcher, state, id = attachment->id] {
        const auto result = watcher->result(); watcher->deleteLater();
        const auto target = state.lock();
        if (!target || !target->attachments.contains(id)) return;
        if (!result.error.isEmpty()) {
            if (target == _active) emit inputRejected(result.error);
        } else {
            target->document->addResource(QTextDocument::ImageResource, QUrl(id), result.preview);
            target->document->markContentsDirty(0, target->document->characterCount());
            if (target == _active) viewport()->update();
        }
        target->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    _active->attachments.insert(attachment->id, attachment);
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
            _active->attachments.insert(it.key(), it.value());
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

void MessageTextEdit::setConversation(int chatId)
{
    if (chatId < 0) return;
    if (_active) _active->cursor = textCursor();
    if (!_drafts.contains(chatId)) {
        auto state = std::make_shared<DraftState>();
        state->document = new QTextDocument(this);
        state->document->setDefaultFont(font());
        state->cursor = QTextCursor(state->document);
        state->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        _drafts.insert(chatId, state);
    }
    _switching = true;
    const QSignalBlocker blocker(this);
    _active = _drafts.value(chatId);
    setDocument(_active->document);
    setTextCursor(_active->cursor);
    _switching = false;
}
bool MessageTextEdit::hasDrafts() const
{
    for (const auto &state : _drafts) if (!state->document->isEmpty()) return true;
    return false;
}
