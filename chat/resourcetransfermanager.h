#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QSet>
#include <QHash>
#include <functional>
#include <memory>

class ResourceTransferManager : public QObject {
    Q_OBJECT
public:
    ResourceTransferManager(QUrl endpoint, int uid, QString token, QString cacheRoot, QObject* parent = nullptr);
    ~ResourceTransferManager() override;
    void upload(const QString& path);
    void download(const QJsonObject& descriptor, bool avatar = false);
    void fetchAvatar(int owner);
    void commitAvatar(const QString &resourceId);
    void cancel();
    bool busy() const { return _busy; }
signals:
    void uploaded(QJsonObject descriptor);
    void downloaded(QString resourceId, QString path);
    void failed(QString reason);
    void progress(qint64 completed, qint64 total);
    void avatarResolved(int owner, QJsonObject descriptor);
    void avatarCommitted(QJsonObject descriptor);
private:
    QNetworkRequest request(const QString& route) const;
    void jsonRequest(const QByteArray& method, const QString& route, const QByteArray& body,
                     std::function<void(QJsonObject)> done, qint64 offset = -1);
    void hashNext();
    void beginUpload();
    void sendNext(qint64 offset);
    void verifyDownload(std::shared_ptr<QFile> file, std::shared_ptr<QCryptographicHash> digest,
                        QString expected, QString path, QString id, quint64 generation, bool finalFile = false);
    void fail(const QString& reason);
    QUrl _endpoint;
    int _uid;
    QString _token, _cache, _uploadId, _checkpoint;
    QNetworkAccessManager _network;
    QSet<QNetworkReply*> _replies;
    QSet<QString> _downloads;
    QHash<int, quint64> _avatarRequests;
    QFile _source;
    QCryptographicHash _hash{QCryptographicHash::Sha256};
    QJsonObject _metadata;
    bool _busy = false;
    quint64 _generation = 0;
};
