#pragma once

#include <QObject>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QThreadPool>
#include "resourcetransfermanager.h"

// One cache per authenticated session. All callbacks die with that session.
class AvatarCache final : public QObject
{
    Q_OBJECT
public:
    AvatarCache(QUrl endpoint, int uid, QString token, QString accountRoot, QObject *parent = nullptr);
    void watch(int owner);
    QImage image(int owner) const { return _images.value(owner); }
    void upload(const QString &path);
    void refresh();
signals:
    void changed(int owner, QImage image);
    void published();
    void uploadFailed(QString error);
private:
    void resolve(int owner, const QJsonObject &descriptor);
    void readImage(int owner, QString path, QString id, bool persist);
    ResourceTransferManager _reader;
    ResourceTransferManager _uploader;
    QString _root;
    int _uid;
    QSet<int> _watched;
    QHash<int, QString> _versions;
    QHash<int, QImage> _images;
    QHash<QString, int> _owners;
    QTimer _refresh;
    QThreadPool _worker;
    bool _publishing = false;
    QString _uploadPath;
    QString _committingId;
};
