#pragma once

#include <QImage>
#include <QString>

struct AvatarResult {
    QImage image;
    QString error;
};

// File operations run on LocalAvatar's worker, never on a widget's thread.
class LocalAvatarStore
{
public:
    explicit LocalAvatarStore(QString root, QString legacyRoot = {});
    static AvatarResult readImage(const QString &fileName);
    AvatarResult load(const QString &environment, int uid) const;
    QString save(const QString &environment, int uid, const QImage &image) const;
    QString path(const QString &environment, int uid) const;
    QString accountRoot(const QString &environment, int uid) const;

private:
    QString _root;
    QString _legacyRoot;
};
