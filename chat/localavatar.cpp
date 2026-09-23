#include "localavatar.h"
#include "avatarcrop.h"

#include <utility>

#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

LocalAvatar::LocalAvatar(QString root, QObject *parent, QString legacyRoot)
    : QObject(parent), _store(std::move(root), std::move(legacyRoot))
{
    // Preserve save/load order when the same account logs in again during a save.
    _worker.setMaxThreadCount(1);
}

void LocalAvatar::setBusy(bool busy)
{
    _busy = busy;
    emit busyChanged(busy);
}

void LocalAvatar::reset()
{
    ++_generation;
    ++_selectionRevision;
    _uid = 0;
    _environment.clear();
    _image = {};
    _selection = {};
    _pendingImage = {};
    _saving = false;
    _selecting = false;
    setBusy(false);
    emit imageChanged(_image);
    emit selectionChanged(_selection);
}

void LocalAvatar::setAccount(const QString &environment, int uid)
{
    reset();
    _environment = environment;
    _uid = uid;
    setBusy(true);
    const auto generation = _generation;
    const auto imageRevision = _imageRevision;
    auto *watcher = new QFutureWatcher<AvatarResult>(this);
    connect(watcher, &QFutureWatcher<AvatarResult>::finished, this,
        /** @brief 仅将当前账号且未被新图片覆盖的加载结果应用到界面。 */
        [this, watcher, generation, imageRevision]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != _generation) {
            return;
        }
        if (_imageRevision == imageRevision) _image = result.image;
        setBusy(false);
        emit imageChanged(_image);
        if (!result.error.isEmpty()) {
            emit errorOccurred(result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run(&_worker,
        /** @brief 在工作线程读取指定环境和账号的头像。 */
        [store = _store, environment, uid]() {
        return store.load(environment, uid);
    }));
}

void LocalAvatar::selectFile(const QString &fileName)
{
    if (fileName.isEmpty() || _busy || _uid == 0) {
        return;
    }
    discardSelection();
    _selecting = true;
    setBusy(true);
    const auto generation = _generation;
    const auto revision = _selectionRevision;
    auto *watcher = new QFutureWatcher<AvatarResult>(this);
    connect(watcher, &QFutureWatcher<AvatarResult>::finished, this,
        /** @brief 仅应用当前账号与选图版本的候选图片。 */
        [this, watcher, generation, revision]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != _generation || revision != _selectionRevision) {
            return;
        }
        _selection = result.image;
        _selecting = false;
        setBusy(false);
        emit selectionChanged(_selection);
        if (!result.error.isEmpty()) {
            emit errorOccurred(result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run(&_worker,
        /** @brief 在工作线程解码用户选择的图片文件。 */
        [fileName]() { return LocalAvatarStore::readImage(fileName); }));
}

void LocalAvatar::discardSelection()
{
    ++_selectionRevision;
    _selection = {};
    emit selectionChanged(_selection);
    // A cancelled preview may finish later, but must not change the next dialog.
    if (_selecting) {
        _selecting = false;
        setBusy(false);
    }
}

void LocalAvatar::saveSelection(const QRectF &sourceRect)
{
    if (_busy || _selection.isNull() || _uid == 0) {
        return;
    }
    const auto generation = _generation;
    const QImage source = _selection;
    const bool upload = _uploadEnabled;
    const QString staged = QDir(_store.accountRoot(_environment, _uid)).filePath(
        "transfers/uploads/avatar-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
    _saving = true;
    setBusy(true);
    auto *watcher = new QFutureWatcher<AvatarResult>(this);
    connect(watcher, &QFutureWatcher<AvatarResult>::finished, this,
        /** @brief 仅为当前账号处理保存结果并决定是否请求上传。 */
        [this, watcher, generation, upload, staged]() {
        const AvatarResult result = watcher->result();
        watcher->deleteLater();
        if (generation != _generation) {
            return;
        }
        if (!result.error.isEmpty()) {
            _saving = false;
            setBusy(false);
            emit errorOccurred(result.error);
            return;
        }
        if (upload) {
            _pendingImage = result.image;
            emit uploadRequested(staged);
            return;
        }
        _saving = false;
        setBusy(false);
        _image = result.image;
        emit imageChanged(_image);
        emit saved();
    });
    watcher->setFuture(QtConcurrent::run(&_worker,
        /** @brief 在工作线程裁剪图片并原子保存本地或待上传文件。 */
        [store = _store, environment = _environment, uid = _uid, source, sourceRect, upload, staged]() -> AvatarResult {
            const QImage cropped = AvatarCrop::render(source, sourceRect);
            if (upload) {
                if (cropped.isNull() || !QDir().mkpath(QFileInfo(staged).absolutePath()))
                    return {{}, tr("无法在安装目录中准备头像，请检查目录写入权限")};
                QSaveFile file(staged);
                if (!file.open(QIODevice::WriteOnly) || !cropped.save(&file, "PNG") || !file.commit())
                    return {{}, tr("无法保存待上传头像")};
                return {cropped, {}};
            }
            return {cropped, store.save(environment, uid, cropped)};
        }));
}

void LocalAvatar::finishUpload(const QString &error)
{
    if (!_saving || _pendingImage.isNull()) return;
    _saving = false;
    setBusy(false);
    if (!error.isEmpty()) { emit errorOccurred(error); return; }
    _image = _pendingImage;
    _pendingImage = {};
    emit imageChanged(_image);
    emit saved();
}

void LocalAvatar::setRemoteImage(const QImage &image)
{
    if (_saving || image.isNull()) return;
    ++_imageRevision;
    _image = image;
    emit imageChanged(_image);
}
