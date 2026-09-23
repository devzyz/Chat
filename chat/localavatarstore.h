#pragma once

#include <QImage>
#include <QString>

/** @brief 携带头像读取结果和错误说明，失败时由调用方显示错误或默认头像。 */
struct AvatarResult {
    QImage image;
    QString error;
};

// File operations run on LocalAvatar's worker, never on a widget's thread.
/** @brief 计算账号头像路径并读写图片文件；文件操作由 LocalAvatar 的工作线程调用。 */
class LocalAvatarStore
{
public:
    /** @brief 初始化对象，用于计算账号头像路径并读写图片文件。 */
    explicit LocalAvatarStore(QString root, QString legacyRoot = {});
    /** @brief 在调用线程解码文件并限制输入，失败返回错误值；应从头像工作线程调用。 */
    static AvatarResult readImage(const QString &fileName);
    /** @brief 读取账号头像，必要时迁移允许的旧头像；失败通过 AvatarResult.error 返回。 */
    AvatarResult load(const QString &environment, int uid) const;
    /** @brief 原子保存账号头像，返回空字符串表示成功，否则为错误原因。 */
    QString save(const QString &environment, int uid, const QImage &image) const;
    /** @brief 返回环境和账号对应的头像文件路径。 */
    QString path(const QString &environment, int uid) const;
    /** @brief 根据环境和账号计算存储根路径；无效身份不应作为有效目录使用。 */
    QString accountRoot(const QString &environment, int uid) const;

private:
    QString _root;
    QString _legacyRoot;
};
