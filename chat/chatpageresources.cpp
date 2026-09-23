#include "chatpage.h"
#include "resourcetransfermanager.h"
#include "messageitemdelegate.h"
#include "ui_chatpage.h"
#include "tcpmgr.h"
#include "usermgr.h"
#include "messageservice.h"
#include <QCoreApplication>
#include <QFileDialog>
#include <QDesktopServices>
#include <QImageReader>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

void ChatPage::initResourceTransfers()
{
    QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    _transfer = new ResourceTransferManager(QUrl(settings.value("ResourceServer/Url", "http://127.0.0.1:8090").toString()),
        UserMgr::instance()->uid(), UserMgr::instance()->token(),
        UserMgr::instance()->storageRoot(), this);
    connect(ui->file_label, &ClickedLabel::clicked, this, &ChatPage::selectResource);
    connect(_transfer, &ResourceTransferManager::failed, this,
        /** @brief 展示资源失败和续传提示并恢复文件按钮。 */
        [this](const QString& reason) {
        ui->file_label->setToolTip(reason + tr("；重新选择同一文件可续传，双击消息可重试下载"));
        ui->file_label->resetNormalState();
    });
    connect(_transfer, &ResourceTransferManager::progress, this,
        /** @brief 按已确认字节更新上传进度提示。 */
        [this](qint64 done, qint64 total) {
        ui->file_label->setToolTip(tr("上传 %1 / %2 字节；再次点击可暂停").arg(done).arg(total));
    });
    connect(_transfer, &ResourceTransferManager::uploaded, this,
        /** @brief 上传完成后以原 UUID 创建资源消息并提交聊天发送。 */
        [this](QJsonObject descriptor) {
        const QString content = "@resource:v1:" + QString::fromUtf8(QJsonDocument(descriptor).toJson(QJsonDocument::Compact));
        auto message = std::make_shared<TextChatData>(_uploadUuid, _uploadChat, ChatType::PRIVATE,
            ChatMessageType::TEXT_TYPE, content, UserMgr::instance()->uid(), QTime::currentTime());
        appendChatMsg(message);
        QJsonObject payload{{"from_uid", UserMgr::instance()->uid()}, {"to_uid", _uploadRecipient},
            {"chat_id", _uploadChat}, {"resource_id", descriptor["resource_id"]},
            {"text_array", QJsonArray{QJsonObject{{"msg_uuid", _uploadUuid}, {"msg_content", content}}}}};
        UserMgr::instance()->messages()->send(payload);
        ui->file_label->setToolTip(tr("上传完成"));
    });
    connect(_transfer, &ResourceTransferManager::downloaded, this,
        /** @brief 下载完成后生成预览并更新对应消息资源路径。 */
        [this](const QString& id, const QString& path) {
        QPixmap preview;
        if (_resourceDescriptors.value(id)["media_type"].toString().startsWith("image/")) {
            QImageReader reader(path);
            reader.setScaledSize(reader.size().scaled(320, 180, Qt::KeepAspectRatio));
            preview = QPixmap::fromImage(reader.read());
        }
        for (int chatId : _resourceChats) {
            if (auto* model = _messageStore.find(chatId)) model->setResourceFile(id, path, preview);
        }
        _messageDelegate->clearSizeCache();
        ui->chat_detail_data_list->doItemsLayout();
        ui->chat_detail_data_list->viewport()->update();
    });
    connect(ui->chat_detail_data_list, &QListView::doubleClicked, this,
        /** @brief 根据双击消息状态选择重试发送、重试下载或打开文件。 */
        [this](const QModelIndex& index) {
        const auto uuid = index.data(MessageListModel::ClientMessageIdRole).toString();
        const auto status = static_cast<DeliveryStatus>(index.data(MessageListModel::DeliveryStatusRole).toInt());
        if ((status == DeliveryStatus::Failed || status == DeliveryStatus::Uncertain)
            && !uuid.isEmpty() && _chatInfo) {
            UserMgr::instance()->messages()->retry(_currentChatId, uuid);
            return;
        }
        const auto id = index.data(MessageListModel::ResourceIdRole).toString();
        const auto path = index.data(MessageListModel::LocalResourcePathRole).toString();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else if (_resourceDescriptors.contains(id)) _transfer->download(_resourceDescriptors.value(id));
    });
}

void ChatPage::loadResource(MessageRecord& record)
{
    if (!record.text.startsWith("@resource:v1:")) return;
    const auto descriptor = QJsonDocument::fromJson(record.text.mid(13).toUtf8()).object();
    record.resourceId = descriptor["resource_id"].toString();
    const auto type = descriptor["media_type"].toString();
    record.messageType = type.startsWith("image/") ? MessageType::Image :
        type.startsWith("video/") ? MessageType::Video : MessageType::File;
    record.text = descriptor["name"].toString() + tr("（双击打开或重试下载）");
    const bool known = _resourceDescriptors.contains(record.resourceId);
    _resourceDescriptors[record.resourceId] = descriptor;
    _resourceChats.insert(record.chatId);
    if (!known) QTimer::singleShot(0, this,
        /** @brief 稍后重试当前描述对应的资源下载。 */
        [this, descriptor] { _transfer->download(descriptor); });
}

void ChatPage::selectResource()
{
    if (!_chatInfo) return;
    if (_transfer->busy()) {
        _transfer->cancel();
        ui->file_label->setToolTip(tr("上传已暂停；重新选择同一文件继续"));
        return;
    }
    const auto path = QFileDialog::getOpenFileName(this, tr("发送文件"), {},
        tr("所有文件 (*);;图片和视频 (*.png *.jpg *.jpeg *.mp4 *.avi)"));
    if (path.isEmpty()) return;
    _uploadChat = _chatInfo->getChatId(); _uploadRecipient = _chatInfo->getUid();
    _uploadUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _transfer->upload(path);
}
