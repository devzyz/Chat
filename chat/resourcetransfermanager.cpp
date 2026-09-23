#include "resourcetransfermanager.h"
#include "userstoragepaths.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QRegularExpression>

namespace { constexpr qint64 blockSize = 64 * 1024; }
ResourceTransferManager::ResourceTransferManager(QUrl endpoint, int uid, QString token, QString cacheRoot, QObject* parent)
    : QObject(parent), _endpoint(std::move(endpoint)), _uid(uid), _token(std::move(token)),
      _cache(std::move(cacheRoot)), _network(this) {}
ResourceTransferManager::~ResourceTransferManager() { cancel(); }
QNetworkRequest ResourceTransferManager::request(const QString& route) const {
    QNetworkRequest value(_endpoint.resolved(QUrl(route)));
    value.setRawHeader("X-User-Id", QByteArray::number(_uid));
    value.setRawHeader("Authorization", "Bearer " + _token.toUtf8());
    value.setTransferTimeout(30000);
    value.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return value;
}
void ResourceTransferManager::cancel() {
    ++_generation;
    const auto replies = _replies; _replies.clear();
    for (auto* reply : replies) { reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    _downloads.clear(); _source.close(); _busy = false;
}
void ResourceTransferManager::fail(const QString& reason) {
    _source.close(); _busy = false; emit failed(reason);
}
void ResourceTransferManager::jsonRequest(const QByteArray& method, const QString& route, const QByteArray& body,
                                         std::function<void(QJsonObject)> done, qint64 offset) {
    auto headers = request(route);
    headers.setHeader(QNetworkRequest::ContentTypeHeader, offset >= 0 ? "application/octet-stream" : "application/json");
    if (offset >= 0) headers.setRawHeader("Upload-Offset", QByteArray::number(offset));
    auto* reply = _network.sendCustomRequest(headers, method, body);
    _replies.insert(reply);
    const auto generation = _generation;
    connect(reply, &QNetworkReply::finished, this,
        /** @brief 释放响应对象，仅向当前任务代交付成功结果或报告失败。 */
        [this, reply, generation, done = std::move(done)] {
        _replies.remove(reply); reply->deleteLater();
        if (generation != _generation) return;
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404 || status == 422) QFile::remove(_checkpoint);
            fail(response.value("error").toString(reply->errorString())); return;
        }
        done(response);
    });
}
void ResourceTransferManager::upload(const QString& path) {
    if (_busy) { emit failed(tr("已有上传任务正在进行")); return; }
    _source.setFileName(path);
    if (!_source.open(QIODevice::ReadOnly) || _source.size() <= 0) { fail(tr("无法打开文件")); return; }
    const auto suffix = QFileInfo(path).suffix().toLower();
    const auto type = suffix == "png" ? "image/png" : (suffix == "jpg" || suffix == "jpeg") ? "image/jpeg" :
                      suffix == "mp4" ? "video/mp4" : suffix == "avi" ? "video/x-msvideo" : "application/octet-stream";
    _busy = true; _hash.reset(); _uploadId.clear();
    _metadata = {{"name", QFileInfo(path).fileName()}, {"media_type", type}, {"size", QString::number(_source.size())}};
    hashNext();
}
void ResourceTransferManager::hashNext() {
    if (!_busy) return;
    const auto bytes = _source.read(blockSize);
    if (bytes.isEmpty() && !_source.atEnd()) { fail(tr("读取文件失败")); return; }
    _hash.addData(bytes);
    if (!_source.atEnd()) {
        const auto generation = _generation;
        QTimer::singleShot(0, this,
            /** @brief 只为仍有效的上传代继续计算摘要。 */
            [this, generation] { if (generation == _generation) hashNext(); });
        return;
    }
    _metadata["sha256"] = QString::fromLatin1(_hash.result().toHex());
    _source.seek(0); beginUpload();
}
void ResourceTransferManager::beginUpload() {
    const auto key = QCryptographicHash::hash((_endpoint.toString() + "\n" + _metadata["media_type"].toString()
        + "\n" + _metadata["sha256"].toString()).toUtf8(),
                                             QCryptographicHash::Sha256).toHex();
    const auto tasks = QDir(_cache).filePath("transfers/uploads");
    if (_cache.isEmpty() || !QDir().mkpath(tasks)) { fail(tr("无法写入安装目录中的用户数据")); return; }
    _checkpoint = QDir(tasks).filePath(QString::fromLatin1(key) + ".upload.json");
    QFile checkpoint(_checkpoint);
    if (checkpoint.open(QIODevice::ReadOnly)) {
        _uploadId = QJsonDocument::fromJson(checkpoint.readAll()).object()["upload_id"].toString();
    }
    if (!_uploadId.isEmpty()) {
        jsonRequest("GET", "/uploads/" + _uploadId, {},
            /** @brief 校验服务端续传偏移后继续上传。 */
            [this](QJsonObject value) {
            bool valid = false; const auto offset = value["offset"].toString().toLongLong(&valid);
            if (!valid || offset < 0 || offset > _source.size()) { fail(tr("服务端返回了无效续传位置")); return; }
            sendNext(offset);
        });
        return;
    }
    jsonRequest("POST", "/uploads", QJsonDocument(_metadata).toJson(QJsonDocument::Compact),
        /** @brief 保存服务端新上传 ID 和断点文件后发送首块。 */
        [this](QJsonObject value) {
        _uploadId = value["upload_id"].toString();
        if (_uploadId.isEmpty()) { fail(tr("服务端未返回上传任务")); return; }
        QSaveFile checkpoint(_checkpoint);
        if (!checkpoint.open(QIODevice::WriteOnly) || checkpoint.write(QJsonDocument(value).toJson()) < 0 || !checkpoint.commit()) {
            fail(tr("保存续传信息失败")); return;
        }
        sendNext(0);
    });
}
void ResourceTransferManager::sendNext(qint64 offset) {
    const auto generation = _generation;
    emit progress(offset, _source.size());
    if (!_busy || generation != _generation) return;
    if (offset == _source.size()) {
        jsonRequest("POST", "/uploads/" + _uploadId + "/complete", {},
            /** @brief 完成资源校验后释放源文件并通知上传成功。 */
            [this](QJsonObject value) {
            _source.close(); _busy = false; emit uploaded(value);
        });
        return;
    }
    if (!_source.seek(offset)) { fail(tr("无法定位文件续传位置")); return; }
    const auto bytes = _source.read(blockSize);
    if (bytes.isEmpty()) { fail(tr("读取上传文件失败")); return; }
    jsonRequest("PATCH", "/uploads/" + _uploadId, bytes,
        /** @brief 核对块确认偏移，正确时发送下一块。 */
        [this, offset, count = bytes.size()](QJsonObject value) {
        bool valid = false; const auto next = value["offset"].toString().toLongLong(&valid);
        if (!valid || next != offset + count) { fail(tr("服务端确认位置不一致")); return; }
        sendNext(next);
    }, offset);
}
void ResourceTransferManager::download(const QJsonObject& descriptor, bool avatar) {
    const auto id = descriptor["resource_id"].toString();
    static const QRegularExpression safeId("^[0-9a-f-]{36}$");
    if (!safeId.match(id).hasMatch() || _downloads.contains(id)) return;
    bool valid = false; const auto size = descriptor["size"].toString().toLongLong(&valid);
    if (!valid || size <= 0) { emit failed(tr("资源大小无效")); return; }
    const auto type = descriptor["media_type"].toString();
    const auto extension = QFileInfo(descriptor["name"].toString()).suffix().toLower();
    static const QRegularExpression safeExtension("^[a-z0-9]{1,10}$");
    const QString suffix = type == "image/png" ? ".png" : type == "image/jpeg" ? ".jpg" :
        type == "video/mp4" ? ".mp4" : type == "video/x-msvideo" ? ".avi" :
        safeExtension.match(extension).hasMatch() ? "." + extension : ".bin";
    const auto directory = avatar ? UserStoragePaths::avatarDirectory(_cache, descriptor["owner"].toInt())
        : QDir(_cache).filePath(type.startsWith("image/") ? "files/images" :
            type.startsWith("video/") ? "files/videos" : "files/documents");
    if (_cache.isEmpty() || directory.isEmpty() || !QDir().mkpath(directory)) {
        emit failed(tr("无法创建安装目录中的资源目录")); return;
    }
    if (avatar && (type != "image/png" || size > 1024 * 1024)) { emit failed(tr("头像元数据无效")); return; }
    const auto path = QDir(directory).filePath(id + suffix);
    if (QFileInfo::exists(path)) {
        auto cached = std::make_shared<QFile>(path);
        if (!cached->open(QIODevice::ReadOnly) || cached->size() != size) {
            emit failed(tr("缓存文件无效，请清理该文件后重试")); return;
        }
        _downloads.insert(id);
        verifyDownload(cached, std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256),
            descriptor["sha256"].toString(), path, id, _generation, true);
        return;
    }
    auto file = std::make_shared<QFile>(path + ".part");
    if (!file->open(QIODevice::ReadWrite)) { emit failed(tr("无法创建下载文件")); return; }
    if (file->size() > size) file->resize(0);
    const auto offset = file->size(); file->seek(offset);
    auto headers = request("/resources/" + id);
    if (offset > 0 && offset < size) headers.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
    if (offset == size) { file->resize(0); file->seek(0); }
    const auto start = file->pos();
    auto* reply = _network.get(headers); reply->setReadBufferSize(blockSize);
    _replies.insert(reply); _downloads.insert(id);
    auto failedWrite = std::make_shared<bool>(false);
    auto drain =
        /** @brief 检查响应范围并有界写入下载文件，保留写失败标志。 */
        [reply, file, failedWrite, start, size] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != (start > 0 ? 206 : 200)) return;
        if (start > 0 && !reply->rawHeader("Content-Range").startsWith("bytes " + QByteArray::number(start) + "-")) {
            *failedWrite = true; reply->abort(); return;
        }
        while (reply->bytesAvailable()) {
            const auto data = reply->read(blockSize);
            if (file->pos() + data.size() > size || file->write(data) != data.size()) {
                *failedWrite = true; reply->abort(); return;
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this,
        /** @brief 下载结束后校验文件长度和网络结果，再进入摘要验证。 */
        [this, reply, file, failedWrite, drain, descriptor, path, id, size] {
        drain(); _replies.remove(reply); reply->deleteLater();
        if (!file->flush() || *failedWrite || reply->error() != QNetworkReply::NoError || file->size() != size) {
            _downloads.remove(id);
            emit failed(tr("下载中断，再次打开可继续下载")); return;
        }
        file->seek(0);
        verifyDownload(file, std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256),
                       descriptor["sha256"].toString(), path, id, _generation);
    });
}

void ResourceTransferManager::fetchAvatar(int owner)
{
    if (owner <= 0) return;
    const auto revision = ++_avatarRequests[owner];
    jsonRequest("GET", "/avatars/" + QString::number(owner), {},
        /** @brief 仅发布该用户最新一次头像查询结果。 */
        [this, owner, revision](QJsonObject value) {
        if (_avatarRequests.value(owner) == revision) emit avatarResolved(owner, value);
    });
}

void ResourceTransferManager::commitAvatar(const QString &resourceId)
{
    const QJsonObject value{{"resource_id", resourceId}};
    jsonRequest("PUT", "/avatars/" + QString::number(_uid), QJsonDocument(value).toJson(QJsonDocument::Compact),
        /** @brief 发布头像提交成功的资源描述。 */
        [this](QJsonObject result) { emit avatarCommitted(result); });
}

void ResourceTransferManager::verifyDownload(std::shared_ptr<QFile> file,
    std::shared_ptr<QCryptographicHash> digest, QString expected, QString path, QString id, quint64 generation, bool finalFile)
{
    if (generation != _generation) return;
    const auto bytes = file->read(blockSize);
    if (bytes.isEmpty() && !file->atEnd()) {
        _downloads.remove(id); emit failed(tr("读取下载文件失败")); return;
    }
    digest->addData(bytes);
    if (!file->atEnd()) {
        QTimer::singleShot(0, this,
            /** @brief 只在匹配任务代时继续分块摘要校验。 */
            [this, file, digest, expected, path, id, generation, finalFile] {
            verifyDownload(file, digest, expected, path, id, generation, finalFile);
        });
        return;
    }
    _downloads.remove(id);
    if (QString::fromLatin1(digest->result().toHex()) != expected) {
        file->close(); file->remove(); emit failed(tr("下载文件校验失败")); return;
    }
    file->close();
    if (!finalFile && !file->rename(path)) { emit failed(tr("保存下载文件失败")); return; }
    emit downloaded(id, path);
}
