#pragma once

#include "localavatarstore.h"

#include <QObject>
#include <QRectF>
#include <QThreadPool>

class LocalAvatar final : public QObject
{
    Q_OBJECT
public:
    explicit LocalAvatar(QString root, QObject *parent = nullptr, QString legacyRoot = {});
    void setAccount(const QString &environment, int uid);
    void reset();
    void selectFile(const QString &fileName);
    void discardSelection();
    void saveSelection(const QRectF &sourceRect);
    QImage image() const { return _image; }
    QImage selection() const { return _selection; }
    bool isBusy() const { return _busy; }
    bool isSaving() const { return _saving; }
    void setUploadEnabled(bool enabled) { _uploadEnabled = enabled; }
    void finishUpload(const QString &error = {});
    void setRemoteImage(const QImage &image);

signals:
    void imageChanged(const QImage &image);
    void selectionChanged(const QImage &image);
    void busyChanged(bool busy);
    void errorOccurred(const QString &error);
    void saved();
    void uploadRequested(const QString &path);

private:
    void setBusy(bool busy);
    LocalAvatarStore _store;
    QThreadPool _worker;
    QString _environment;
    int _uid = 0;
    quint64 _generation = 0;
    quint64 _selectionRevision = 0;
    quint64 _imageRevision = 0;
    QImage _image;
    QImage _selection;
    bool _busy = false;
    bool _saving = false;
    bool _selecting = false;
    bool _uploadEnabled = false;
    QImage _pendingImage;
};
