#include "userstoragepaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QUrl>

QString UserStoragePaths::dataRoot()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

QString UserStoragePaths::environmentKey(const QString &environment)
{
    QUrl url(environment.trimmed());
    url.setFragment({});
    if (url.path() == "/") url.setPath({});
    const QString normalized = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString();
    return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString UserStoragePaths::accountRoot(const QString &root, const QString &environment, int uid)
{
    if (uid <= 0 || environment.isEmpty() || root.isEmpty()) return {};
    return QDir(root).filePath(QStringLiteral("environments/%1/users/%2").arg(environmentKey(environment)).arg(uid));
}

QString UserStoragePaths::avatarDirectory(const QString &accountRoot, int owner)
{
    if (accountRoot.isEmpty() || owner <= 0) return {};
    return QDir(accountRoot).filePath(QStringLiteral("static/head/%1").arg(owner));
}
