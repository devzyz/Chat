#include "messagesubmissioncontroller.h"
#include "messageservice.h"
#include "resourcetransfermanager.h"
#include "chattcptransport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

namespace {
/** @brief 检查持久化预算及最大 attempt 字段加入后的线协议预算。 */
bool fitsRequest(QJsonObject request)
{
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > 1950) return false;
    request["attempt_id"] = QStringLiteral("9223372036854775807");
    return QJsonDocument(request).toJson(QJsonDocument::Compact).size() <= ChatTcpTransport::maxBodyBytes();
}
/** @brief 构造单消息请求，保留调用者固定的会话身份。 */
QJsonObject messageRequest(QJsonObject context, const QString &uuid, const QString &content)
{
    context["text_array"] = QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"msg_content", content}}};
    return context;
}
}

MessageSubmissionController::MessageSubmissionController(MessageService *messages,
    ResourceTransferManager *uploads, int uid, QObject *parent)
    : QObject(parent), _messages(messages), _uploads(uploads), _uid(uid)
{
    connect(uploads, &ResourceTransferManager::uploaded, this,
        /** @brief 上传完成后保留描述，存储失败无需重复上传。 */ [this](const QJsonObject &descriptor) {
        if (!_uploading || !hasPending()) return;
        _uploading = false;
        auto request = messageRequest(_context, _uploadUuid,
            "@resource:v1:" + QString::fromUtf8(QJsonDocument(descriptor).toJson(QJsonDocument::Compact)));
        request["resource_id"] = descriptor["resource_id"];
        if (!fitsRequest(request)) { fail(tr("附件描述超过消息大小限制，请缩短文件名后重新提交")); return; }
        _requests = {request}; persist();
    });
    connect(uploads, &ResourceTransferManager::failed, this,
        /** @brief 专用上传器失败只影响当前草稿。 */ [this](const QString &reason) {
        if (!_uploading) return;
        _uploading = false; fail(reason);
    });
    connect(uploads, &ResourceTransferManager::progress, this,
        /** @brief 发布当前固定目标的上传进度。 */ [this](qint64 done, qint64 total) {
        if (!_uploading) return;
        _status = tr("会话 %1：上传 %2 / %3 字节").arg(_context["chat_id"].toInt()).arg(done).arg(total);
        emit stateChanged();
    });
    connect(messages, &MessageService::outgoingPersisted, this,
        /** @brief 只消费当前 UUID 的落盘结果，成功部分永久移交 outbox。 */
        [this](int chat, const QVector<QString> &ids, bool success) {
        if (!_saving || _requests.isEmpty() || chat != _context["chat_id"].toInt()) return;
        const auto uuid = _requests.first()["text_array"].toArray().first().toObject()["msg_uuid"].toString();
        if (!ids.contains(uuid)) return;
        _saving = false;
        if (_cancelAfterSave) { finish(); return; }
        if (!success) { fail(tr("消息本地保存失败，内容已保留，可重试或取消")); return; }
        _requests.removeFirst();
        if (_requests.isEmpty()) { _entries.removeFirst(); _uploadUuid.clear(); }
        QTimer::singleShot(0, this, &MessageSubmissionController::advance);
    });
    connect(messages, &MessageService::stopped, this,
        /** @brief 账号结束使上传和未落盘任务失效。 */ [this] {
        _uploads->cancel(); finish();
    });
}

QVector<QJsonObject> MessageSubmissionController::textRequests(const QString &text, const QJsonObject &context)
{
    QVector<QJsonObject> result;
    QString chunk;
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (qsizetype i = 0; i < text.size();) {
        const qsizetype width = text.at(i).isHighSurrogate() && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate() ? 2 : 1;
        const QString character = text.mid(i, width);
        if (!fitsRequest(messageRequest(context, uuid, chunk + character))) {
            if (chunk.isEmpty()) return {};
            result.push_back(messageRequest(context, uuid, chunk));
            chunk.clear(); uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!fitsRequest(messageRequest(context, uuid, character))) return {};
        }
        chunk += character; i += width;
    }
    if (!chunk.isEmpty()) result.push_back(messageRequest(context, uuid, chunk));
    return result;
}

bool MessageSubmissionController::submit(const MessageDraft &draft, int chatId, int recipient, bool group)
{
    if (!draft.error.isEmpty()) { emit rejected(draft.error); return false; }
    if (hasPending() || draft.id.isEmpty() || draft.id == _lastDraft || draft.entries.isEmpty()) {
        emit rejected(tr("当前提交尚未完成或草稿为空")); return false;
    }
    if (!_messages->isActive() || chatId <= 0 || (!group && recipient <= 0)) {
        emit rejected(tr("尚未建立有效会话")); return false;
    }
    QJsonObject context{{"from_uid", _uid}, {"to_uid", group ? 0 : recipient}, {"chat_id", chatId}};
    if (group) {
        const auto state = _messages->groupState(chatId);
        if (state["group_state"] != "active") { emit rejected(tr("当前群不可发送")); return false; }
        context["chat_type"] = "group"; context["membership_epoch"] = state["membership_epoch"];
    }
    if (!group) {
        const auto state = _messages->privateState(chatId);
        if (!_messages->socialReady() || (!state.isEmpty() && !state["relationship_active"].toBool())) {
            emit rejected(tr("好友关系尚未确认或已解除，草稿已保留")); return false;
        }
        if (state.contains("relationship_revision")) context["relationship_revision"] = state["relationship_revision"];
    }
    for (const auto &entry : draft.entries) {
        if (entry.kind == DraftEntry::Kind::Attachment) {
            const QFileInfo file(entry.content);
            if (!file.isFile() || !file.isReadable() || file.size() == 0) {
                emit rejected(tr("附件不存在、为空或不可读，草稿已保留")); return false;
            }
        } else if (!entry.content.isEmpty() && textRequests(entry.content, context).isEmpty()) {
            emit rejected(tr("消息无法满足协议长度限制")); return false;
        }
    }
    _lastDraft = draft.id; _context = context; _entries = draft.entries;
    _status = tr("会话 %1：正在提交").arg(chatId); emit stateChanged();
    QTimer::singleShot(0, this, &MessageSubmissionController::advance);
    return true;
}

void MessageSubmissionController::advance()
{
    if (_saving || _uploading || _failed) return;
    if (_entries.isEmpty()) { finish(); return; }
    if (!_requests.isEmpty()) { persist(); return; }
    const auto &entry = _entries.first();
    if (entry.kind == DraftEntry::Kind::Text) {
        _requests = textRequests(entry.content, _context);
        if (_requests.isEmpty()) { _entries.removeFirst(); advance(); return; }
        persist();
    } else {
        if (_uploadUuid.isEmpty()) _uploadUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        _uploading = true; _uploads->upload(entry.content);
    }
}

void MessageSubmissionController::persist()
{
    _saving = true;
    _messages->send(_requests.first(), MessageService::DraftRetention::Caller);
}
void MessageSubmissionController::fail(const QString &reason)
{
    _failed = true; _status = tr("会话 %1：%2").arg(_context["chat_id"].toInt()).arg(reason); emit stateChanged();
}
void MessageSubmissionController::retry()
{
    if (!_failed) return;
    _failed = false; _status = tr("正在重试"); emit stateChanged(); advance();
}
void MessageSubmissionController::cancel()
{
    if (_saving) { _cancelAfterSave = true; return; }
    _uploads->cancel(); finish();
}
void MessageSubmissionController::finish()
{
    _entries.clear(); _requests.clear(); _uploadUuid.clear();
    _saving = false; _uploading = false; _failed = false; _cancelAfterSave = false;
    _status.clear(); emit stateChanged();
}

QString MessageSubmissionController::pendingSummary() const
{
    QStringList lines;
    for (const auto &entry : _entries) lines.push_back(entry.kind == DraftEntry::Kind::Text
        ? entry.content.left(250) : QFileInfo(entry.content).fileName());
    return lines.join("\n");
}
