#pragma once
#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include "userdata.h"
/** @brief 管理一次用户搜索身份及有限等待；网络适配由组装方提供。 */
class UserSearchController final : public QObject {
    Q_OBJECT
public:
    /** @brief 创建搜索流程，默认超时十秒；测试可传更短期限。 */
    explicit UserSearchController(QObject *parent = nullptr, int timeoutMs = 10000);
    /** @brief 开始新搜索并替代旧等待，旧响应无法完成新请求。 */
    void search(const QString &text);
    /** @brief 接收网络 JSON；只有当前请求编号可以完成等待。 */
    void accept(const QJsonObject &response);
    /** @brief 取消当前搜索，停止超时并丢弃后续响应。 */
    void cancel();
    /** @brief 断线时结束等待并显示可恢复原因。 */
    void disconnected();
signals:
    /** @brief 请求适配器发送已附请求身份的查询。 */
    void requestReady(QJsonObject request);
    /** @brief 等待状态发生变化。 */
    void busyChanged(bool busy);
    /** @brief 当前请求返回用户资料。 */
    void found(std::shared_ptr<SearchInfo> info);
    /** @brief 当前请求失败，包含可展示原因。 */
    void failed(QString reason);
private:
    /** @brief 当前请求终止并报告失败。 */
    void fail(const QString &reason);
    QTimer _deadline;
    QString _request;
};
