#include "avatarcache.h"
#include "localavatarstore.h"
#include "userstoragepaths.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtConcurrentRun>

AvatarCache::AvatarCache(QUrl endpoint, int uid, QString token, QString accountRoot, QObject *parent)
    : QObject(parent), _reader(endpoint, uid, token, accountRoot),
      _uploader(endpoint, uid, token, accountRoot), _root(std::move(accountRoot)), _uid(uid)
{
    _worker.setMaxThreadCount(1);
    connect(&_reader, &ResourceTransferManager::avatarResolved, this, &AvatarCache::resolve);
    connect(&_reader, &ResourceTransferManager::downloaded, this,
        /** @brief 只读取仍匹配当前资源版本的下载结果。 */
        [this](const QString &id, const QString &path) {
        const int owner = _owners.value(id);
        if (owner > 0 && _versions.value(owner) == id) readImage(owner, path, id, true);
    });
    connect(&_uploader, &ResourceTransferManager::uploaded, this,
        /** @brief 上传成功后请求发布为本人头像。 */
        [this](const QJsonObject &value) {
        _uploader.commitAvatar(value["resource_id"].toString());
    });
    connect(&_uploader, &ResourceTransferManager::avatarCommitted, this,
        /** @brief 发布完成后取消旧查询，校验归属并读取新头像缓存。 */
        [this](const QJsonObject &value) {
        _reader.cancel(); // Discard lookups started before the new profile was committed.
        _committingId = value["resource_id"].toString();
        static const QRegularExpression safeId("^[0-9a-f-]{36}$");
        if (!safeId.match(_committingId).hasMatch() || value["owner"].toInt() != _uid) {
            _publishing = false;
            emit uploadFailed(tr("服务端未返回头像资源"));
            return;
        }
        _versions[_uid] = _committingId;
        readImage(_uid, _uploadPath, _committingId, true);
    });
    connect(&_uploader, &ResourceTransferManager::failed, this,
        /** @brief 结束发布状态并报告上传错误。 */
        [this](const QString &error) {
        _publishing = false;
        emit uploadFailed(error);
    });
    _refresh.setInterval(30000);
    connect(&_refresh, &QTimer::timeout, this, &AvatarCache::refresh);
    _refresh.start();
}

void AvatarCache::watch(int owner)
{
    if (owner <= 0 || _watched.contains(owner)) return;
    _watched.insert(owner);
    const auto directory = UserStoragePaths::avatarDirectory(_root, owner);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
        /** @brief 读取本地索引完成后加载缓存并查询远端头像。 */
        [this, watcher, owner, directory] {
        const auto id = watcher->result(); watcher->deleteLater();
        if (!id.isEmpty() && !_versions.contains(owner)) {
            _versions[owner] = id;
            readImage(owner, QDir(directory).filePath(id + ".png"), id, false);
        }
        _reader.fetchAvatar(owner);
    });
    watcher->setFuture(QtConcurrent::run(&_worker,
        /** @brief 在工作线程读取并校验头像索引中的资源 ID。 */
        [directory] {
        QFile file(QDir(directory).filePath("index.json"));
        if (!file.open(QIODevice::ReadOnly)) return QString();
        const auto id = QJsonDocument::fromJson(file.readAll()).object()["resource_id"].toString();
        static const QRegularExpression safe("^[0-9a-f-]{36}$");
        return safe.match(id).hasMatch() ? id : QString();
    }));
}

void AvatarCache::resolve(int owner, const QJsonObject &descriptor)
{
    if (_publishing && owner == _uid) return;
    const auto id = descriptor["resource_id"].toString();
    if (id.isEmpty() || descriptor["owner"].toInt() != owner) return;
    if (_versions.value(owner) == id && _images.contains(owner)) return;
    _versions[owner] = id;
    _owners[id] = owner;
    _reader.download(descriptor, true);
}

void AvatarCache::readImage(int owner, QString path, QString id, bool persist)
{
    auto *watcher = new QFutureWatcher<AvatarResult>(this);
    connect(watcher, &QFutureWatcher<AvatarResult>::finished, this,
        /** @brief 仅应用当前版本的解码结果并完成头像发布通知。 */
        [this, watcher, owner, id] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (_versions.value(owner) != id) return;
        if (!result.error.isEmpty() || result.image.size() != QSize(256, 256)) {
            if (owner == _uid && id == _committingId) {
                _publishing = false;
                _committingId.clear();
                emit uploadFailed(tr("头像已发布，但本地缓存失败：%1").arg(result.error));
            }
            return;
        }
        _images[owner] = result.image;
        emit changed(owner, result.image);
        if (owner == _uid && id == _committingId) {
            _publishing = false;
            _committingId.clear();
            emit published();
        }
    });
    const auto directory = UserStoragePaths::avatarDirectory(_root, owner);
    watcher->setFuture(QtConcurrent::run(&_worker,
        /** @brief 在工作线程读取图片，按需原子保存图片和索引。 */
        [path, id, persist, directory] {
        auto result = LocalAvatarStore::readImage(path);
        if (result.error.isEmpty() && result.image.size() == QSize(256, 256) && persist) {
            const auto target = QDir(directory).filePath(id + ".png");
            if (path != target) {
                QSaveFile image(target);
                QFile source(path);
                if (!QDir().mkpath(directory) || !source.open(QIODevice::ReadOnly) || !image.open(QIODevice::WriteOnly))
                    return AvatarResult{{}, QObject::tr("无法创建本地头像缓存")};
                const auto bytes = source.readAll();
                if (image.write(bytes) != bytes.size() || !image.commit())
                    return AvatarResult{{}, QObject::tr("无法保存本地头像缓存")};
            }
            QSaveFile index(QDir(directory).filePath("index.json"));
            const auto bytes = QJsonDocument(QJsonObject{{"resource_id", id}}).toJson(QJsonDocument::Compact);
            if (!index.open(QIODevice::WriteOnly) || index.write(bytes) != bytes.size() || !index.commit())
                result.error = QObject::tr("无法保存头像索引");
            if (result.error.isEmpty() && path != target) QFile::remove(path);
        }
        return result;
    }));
}

void AvatarCache::upload(const QString &path)
{
    _publishing = true;
    _uploadPath = path;
    _uploader.upload(path);
}

void AvatarCache::refresh()
{
    for (int owner : _watched) _reader.fetchAvatar(owner);
}
