#pragma once

#include "localavatarstore.h"

#include <QObject>
#include <QRectF>
#include <QThreadPool>

/** @brief 协调账号头像异步读写；通过账号代号及图片版本丢弃旧任务结果。 */
class LocalAvatar final : public QObject
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于协调账号头像异步读写。 */
    explicit LocalAvatar(QString root, QObject *parent = nullptr, QString legacyRoot = {});
    /** @brief 切换环境和账号并增加任务代号，异步加载该账号头像，旧账号任务结果不再应用。 */
    void setAccount(const QString &environment, int uid);
    /** @brief 清除当前账号关联并使旧任务失效，保留磁盘缓存。 */
    void reset();
    /** @brief 异步读取候选图片，只有仍匹配当前账号和选图版本的结果会应用。 */
    void selectFile(const QString &fileName);
    /** @brief 丢弃当前候选图并使旧选图结果失效。 */
    void discardSelection();
    /** @brief 按 sourceRect 裁剪并异步原子保存，按配置继续请求远端上传。 */
    void saveSelection(const QRectF &sourceRect);
    /** @brief 返回当前缓存图片值，尚未加载时可能为空。 */
    QImage image() const { return _image; }
    /** @brief 返回当前待裁剪的候选图片值。 */
    QImage selection() const { return _selection; }
    /** @brief 查询头像是否正在读写或等待异步操作完成。 */
    bool isBusy() const { return _busy; }
    /** @brief 查询头像是否正在保存或等待上传结果。 */
    bool isSaving() const { return _saving; }
    /** @brief 设置本地头像保存后是否还需等待远端上传发布。 */
    void setUploadEnabled(bool enabled) { _uploadEnabled = enabled; }
    /** @brief 应用头像上传结果；error 为空表示成功，否则保留失败提示。 */
    void finishUpload(const QString &error = {});
    /** @brief 更新已发布头像图片，同时防止覆盖较新的本地编辑结果。 */
    void setRemoteImage(const QImage &image);

signals:
    /** @brief 通知当前账号展示头像已更新。 */
    void imageChanged(const QImage &image);
    /** @brief 通知候选图片变化，供裁剪控件刷新。 */
    void selectionChanged(const QImage &image);
    /** @brief 通知异步忙碌状态变化，供界面限制重复操作。 */
    void busyChanged(bool busy);
    /** @brief 通知头像读写或上传失败的用户可见原因。 */
    void errorOccurred(const QString &error);
    /** @brief 通知头像保存流程成功结束。 */
    void saved();
    /** @brief 本地裁剪图片已保存后，通知上层上传给定文件。 */
    void uploadRequested(const QString &path);

private:
    /** @brief 更新异步忙碌状态并在变化时发出通知。 */
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
