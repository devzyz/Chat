#include "localavatarstore.h"
#include "userstoragepaths.h"

#include <utility>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QObject>
#include <QSaveFile>

LocalAvatarStore::LocalAvatarStore(QString root, QString legacyRoot)
    : _root(std::move(root)), _legacyRoot(std::move(legacyRoot)) {}

AvatarResult LocalAvatarStore::readImage(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, QObject::tr("无法读取图片：%1").arg(file.errorString())};
    }
    if (file.size() > 5 * 1024 * 1024) {
        return {{}, QObject::tr("请选择不超过 5 MB 的图片")};
    }
    QImageReader reader(&file);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg") {
        return {{}, QObject::tr("请选择有效的 JPG 或 PNG 图片")};
    }
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096) {
        return {{}, QObject::tr("图片宽高不能超过 4096 像素")};
    }
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        return {{}, QObject::tr("图片已损坏或无法解码")};
    }
    return {image, {}};
}

QString LocalAvatarStore::path(const QString &environment, int uid) const
{
    const auto directory = UserStoragePaths::avatarDirectory(accountRoot(environment, uid), uid);
    return directory.isEmpty() ? QString() : QDir(directory).filePath("current.png");
}

QString LocalAvatarStore::accountRoot(const QString &environment, int uid) const
{
    return UserStoragePaths::accountRoot(_root, environment, uid);
}

AvatarResult LocalAvatarStore::load(const QString &environment, int uid) const
{
    const QString fileName = path(environment, uid);
    if (!QFileInfo::exists(fileName)) {
        if (!_legacyRoot.isEmpty() && uid > 0 && !environment.isEmpty()) {
            const auto key = QCryptographicHash::hash(environment.toUtf8(), QCryptographicHash::Sha256).toHex();
            const auto legacy = QDir(_legacyRoot).filePath(QStringLiteral("avatars/v1/%1/%2.png")
                .arg(QString::fromLatin1(key)).arg(uid));
            if (QFileInfo::exists(legacy)) {
                auto result = readImage(legacy);
                if (result.error.isEmpty()) result.error = save(environment, uid, result.image);
                return result; // Preserve the legacy file even after a successful migration.
            }
        }
        return {};
    }
    return readImage(fileName);
}

QString LocalAvatarStore::save(const QString &environment, int uid, const QImage &image) const
{
    if (image.isNull() || image.size() != QSize(256, 256)) {
        return QObject::tr("请先选择有效图片");
    }
    const QString fileName = path(environment, uid);
    if (fileName.isEmpty()) return QObject::tr("无效的用户数据目录");
    if (!QDir().mkpath(QFileInfo(fileName).absolutePath())) {
        return QObject::tr("无法创建头像保存目录");
    }
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        return QObject::tr("头像保存失败：%1").arg(file.errorString());
    }
    return {};
}
