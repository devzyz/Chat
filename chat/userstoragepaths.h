#pragma once

#include <QString>

// root is the installation's data directory; tests supply a disposable root.
/** @brief 集中计算安装目录下按环境和账号隔离的存储路径。 */
class UserStoragePaths
{
public:
    /** @brief 返回安装目录下的统一数据根路径。 */
    static QString dataRoot();
    /** @brief 由服务端环境生成稳定目录键，隔离不同部署的数据。 */
    static QString environmentKey(const QString &environment);
    /** @brief 根据环境和账号计算存储根路径；无效身份不应作为有效目录使用。 */
    static QString accountRoot(const QString &root, const QString &environment, int uid);
    /** @brief 返回账号缓存下指定用户的头像目录。 */
    static QString avatarDirectory(const QString &accountRoot, int owner);
};
