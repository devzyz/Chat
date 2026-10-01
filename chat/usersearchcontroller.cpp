#include "usersearchcontroller.h"
#include <QUuid>
UserSearchController::UserSearchController(QObject *parent, int timeoutMs) : QObject(parent)
{
    _deadline.setSingleShot(true); _deadline.setInterval(qMax(1, timeoutMs));
    connect(&_deadline, &QTimer::timeout, this,
        /** @brief 无响应也必须结束等待，用户可发起带新编号的重试。 */ [this] { fail(tr("搜索超时，请重试")); });
}
void UserSearchController::search(const QString &text)
{
    cancel();
    if (text.trimmed().isEmpty()) { emit failed(tr("请输入用户编号或名称")); return; }
    _request = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _deadline.start(); emit busyChanged(true);
    emit requestReady({{"uid_name", text}, {"request_id", _request}});
}
void UserSearchController::accept(const QJsonObject &response)
{
    if (_request.isEmpty()) return;
    const auto id = response["request_id"].toString();
    if (id.isEmpty()) { fail(tr("服务器未返回搜索编号，请更新服务器以支持安全搜索重试")); return; }
    if (id != _request) return;
    if (!response["error"].isDouble()) { fail(tr("搜索响应格式错误")); return; }
    if (response["error"].toInt(-1) != 0) { fail(tr("未找到用户或搜索失败，请检查输入后重试")); return; }
    if (response["uid"].toInt() <= 0 || !response["name"].isString()) { fail(tr("搜索用户资料无效")); return; }
    auto info = std::make_shared<SearchInfo>(response["uid"].toInt(), response["name"].toString(),
        response["description"].toString(), response["icon"].toString(), response["sex"].toInt());
    cancel(); emit found(info);
}
void UserSearchController::cancel()
{
    _deadline.stop();
    if (_request.isEmpty()) return;
    _request.clear(); emit busyChanged(false);
}
void UserSearchController::disconnected()
{
    if (!_request.isEmpty()) fail(tr("连接已断开，请重新登录后搜索"));
}
void UserSearchController::fail(const QString &reason)
{
    cancel(); emit failed(reason);
}
