#pragma once

#include <QObject>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QThreadPool>
#include "resourcetransfermanager.h"

// One cache per authenticated session. All callbacks die with that session.
/** @brief 管理单次认证会话的远端头像缓存；在所属 Qt 线程调用，销毁后不再接收回调。 */
class AvatarCache final : public QObject
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于管理单次认证会话的远端头像缓存。 */
    AvatarCache(QUrl endpoint, int uid, QString token, QString accountRoot, QObject *parent = nullptr);
    /** @brief 订阅指定用户头像并启动缓存或远端解析。 */
    void watch(int owner);
    /** @brief 返回当前缓存图片值，尚未加载时可能为空。 */
    QImage image(int owner) const { return _images.value(owner); }
    /** @brief 保存待发布的头像路径并委托资源上传器上传，完成后继续发布头像。 */
    void upload(const QString &path);
    /** @brief 重新查询已订阅用户的远端头像版本。 */
    void refresh();
signals:
    /** @brief 通知指定用户可展示的头像图片已变化。 */
    void changed(int owner, QImage image);
    /** @brief 通知当前账号新头像已上传并发布。 */
    void published();
    /** @brief 通知头像上传或发布失败，错误供界面展示。 */
    void uploadFailed(QString error);
private:
    /** @brief 核对远端头像版本，决定复用本地缓存或下载新资源。 */
    void resolve(int owner, const QJsonObject &descriptor);
    /** @brief 在工作线程读取头像，完成后仅向仍有效的账号和版本发布结果。 */
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
