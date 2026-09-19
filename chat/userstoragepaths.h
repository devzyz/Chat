#pragma once

#include <QString>

// root is the installation's data directory; tests supply a disposable root.
class UserStoragePaths
{
public:
    static QString dataRoot();
    static QString environmentKey(const QString &environment);
    static QString accountRoot(const QString &root, const QString &environment, int uid);
    static QString avatarDirectory(const QString &accountRoot, int owner);
};
