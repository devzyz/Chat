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

/** @brief 拥有账号内上传、下载及头像 HTTP 请求；仅在所属 Qt 线程调用，取消时使旧代回调失效。 */
class ResourceTransferManager : public QObject {
    Q_OBJECT
public:
    /** @brief 初始化对象，用于拥有账号内上传、下载及头像 HTTP 请求。 */
    ResourceTransferManager(QUrl endpoint, int uid, QString token, QString cacheRoot, QObject* parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ResourceTransferManager() override;
    /** @brief 开始读取并分块计算文件摘要，再创建或恢复上传任务；已有上传时发出失败信号。 */
    void upload(const QString& path);
    /** @brief 按描述下载或恢复资源并校验摘要；avatar 选择头像目录，重复任务不重复启动。 */
    void download(const QJsonObject& descriptor, bool avatar = false);
    /** @brief 查询指定用户当前头像资源，按请求代号丢弃旧响应。 */
    void fetchAvatar(int owner);
    /** @brief 将已经上传完成的资源发布为当前账号头像。 */
    void commitAvatar(const QString &resourceId);
    /** @brief 使当前任务代失效并取消在途网络请求，保留可用于下次恢复的断点文件。 */
    void cancel();
    /** @brief 查询是否正在处理上传任务；不表示没有并发下载。 */
    bool busy() const { return _busy; }
signals:
    /** @brief 通知上传完成并给出服务器资源描述，后续消息提交由调用方处理。 */
    void uploaded(QJsonObject descriptor);
    /** @brief 通知资源下载和校验完成，给出本地路径。 */
    void downloaded(QString resourceId, QString path);
    /** @brief 通知资源操作失败的可展示原因。 */
    void failed(QString reason);
    /** @brief 通知当前上传已确认及总字节数。 */
    void progress(qint64 completed, qint64 total);
    /** @brief 返回指定用户当前头像描述，供缓存决定是否下载新版本。 */
    void avatarResolved(int owner, QJsonObject descriptor);
    /** @brief 通知新头像已在服务器发布成功。 */
    void avatarCommitted(QJsonObject descriptor);
private:
    /** @brief 构造带账号认证头和传输超时的资源请求，重定向由调用方显式处理。 */
    QNetworkRequest request(const QString& route) const;
    /** @brief 发起 JSON 或分块请求；成功时在对象线程调用 done，失败发出 failed，旧任务代不回调。 */
    void jsonRequest(const QByteArray& method, const QString& route, const QByteArray& body,
                     std::function<void(QJsonObject)> done, qint64 offset = -1);
    /** @brief 读取下一块文件计算摘要，通过事件队列分片执行以避免一次占用整个文件处理时间。 */
    void hashNext();
    /** @brief 根据文件摘要和端点读取断点信息，查询已有任务或创建新上传。 */
    void beginUpload();
    /** @brief 从服务端已确认 offset 读取并上传下一块，到末尾时请求完成校验。 */
    void sendNext(qint64 offset);
    /** @brief 分块校验下载文件摘要，generation 不匹配时放弃旧任务，完成后发布本地路径。 */
    void verifyDownload(std::shared_ptr<QFile> file, std::shared_ptr<QCryptographicHash> digest,
                        QString expected, QString path, QString id, quint64 generation, bool finalFile = false);
    /** @brief 结束当前上传文件操作并发出用户可见失败原因。 */
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
