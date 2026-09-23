#ifndef GATEHTTPTRANSPORT_H
#define GATEHTTPTRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QUrl>
#include <QtGlobal>

#include <memory>

/** @brief 携带 HTTP 地址、请求体、认证流程标识及毫秒级总期限。 */
struct GateHttpRequest
{
    QUrl url;
    QByteArray body;
    quint64 flowId = 0;
    int requestId = -1;
    int module = -1;
    int deadlineMs = 0;
};

enum class GateHttpTerminal
{
    Success,
    NetworkError,
    DeadlineExceeded,
    Cancelled,
    MalformedResponse,
    ResponseTooLarge
};

/** @brief 保存一次 HTTP 操作的终态和响应体，并保留流程及模块关联信息。 */
struct GateHttpResult
{
    quint64 flowId = 0;
    int requestId = -1;
    int module = -1;
    GateHttpTerminal terminal = GateHttpTerminal::NetworkError;
    QByteArray body;
};

/** @brief 在所属 Qt 线程拥有 HTTP 请求，限制响应大小并为每个操作发布终态。 */
class GateHttpTransport final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(GateHttpTransport)

public:
    /** @brief 返回 HTTP 响应允许的最大字节数。 */
    static constexpr qsizetype maxResponseBytes() noexcept { return 8192; }

    /** @brief 初始化对象，用于在所属 Qt 线程拥有 HTTP 请求，限制响应大小并为每个操作发布终态。 */
    explicit GateHttpTransport(QObject *parent = nullptr);
    /** @brief 销毁独占实现并取消在途 HTTP 请求，不发布析构期间的终态。 */
    ~GateHttpTransport() override;

    /** @brief 异步提交带期限的 HTTP 请求，终态通过 finished 通知。 */
    void post(const GateHttpRequest &request);
    /** @brief 取消指定 flowId 的请求并发布取消终态。 */
    void cancel(quint64 flowId);
    /** @brief 取消并清理所有未完成请求，旧请求不会成为新流程结果。 */
    void reset();

signals:
    /** @brief 通知一次传输操作进入终态，携带原流程关联及终止原因。 */
    void finished(const GateHttpResult &result);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // GATEHTTPTRANSPORT_H
