#pragma once
#include <QJsonObject>
#include <QObject>
/** @brief 将社交操作结果转为可恢复提示，不自动重放修改请求。 */
inline QString socialResultText(const QJsonObject &response)
{
    if (response["error"].toInt(-1) == 0) return QObject::tr("已保存");
    const auto reason = response["social_error"].toString();
    if (reason == "VersionConflict") return QObject::tr("资料或好友关系已变化，请刷新后重新操作");
    if (reason == "NameExists") return QObject::tr("用户名已被使用，请修改后重试");
    if (reason == "UpgradeRequired") return QObject::tr("服务器不支持此功能，请更新服务器");
    if (reason == "Timeout") return QObject::tr("结果尚未确认，请刷新后查看，输入已保留");
    if (reason == "InvalidRequest") return QObject::tr("请检查用户名、描述和备注（最多255个字符）");
    return QObject::tr("操作未完成，请稍后重试，输入已保留");
}
