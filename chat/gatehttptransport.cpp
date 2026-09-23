#include "gatehttptransport.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <utility>

/** @brief 独占当前 HTTP 请求、响应缓冲和期限计时器；只在外层 QObject 线程操作。 */
struct GateHttpTransport::Impl
{
    /** @brief 保存借用的拥有者并初始化本次传输运行状态。 */
    explicit Impl(GateHttpTransport *owner)
        : owner(owner)
    {
        manager.setProxy(QNetworkProxy::NoProxy);
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, owner,
            /** @brief 将当前请求超时转为唯一终态并取消网络操作。 */
            [this] {
            finish(generation, GateHttpTerminal::DeadlineExceeded, true);
        });
    }

    /** @brief 取消内部网络操作并释放连接，外层对象不能再收到旧回调。 */
    ~Impl()
    {
        abandon(false);
    }

    /** @brief 替换当前 HTTP 操作并校验请求参数，启动响应读取与总期限。 */
    void post(const GateHttpRequest &next)
    {
        if (active) {
            finish(generation, GateHttpTerminal::Cancelled, true);
        }

        ++generation;
        request = next;
        response.clear();

        if (request.flowId == 0 || request.requestId < 0 || request.module < 0
            || request.deadlineMs <= 0 || !request.url.isValid()
            || (request.url.scheme() != QStringLiteral("http")
                && request.url.scheme() != QStringLiteral("https"))) {
            emitResult(GateHttpTerminal::NetworkError, {});
            return;
        }

        QNetworkRequest networkRequest(request.url);
        networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                                 QByteArrayLiteral("application/json"));
        networkRequest.setHeader(QNetworkRequest::ContentLengthHeader,
                                 request.body.size());

        active = true;
        QNetworkReply *current = manager.post(networkRequest, request.body);
        reply = current;
        const quint64 currentGeneration = generation;

        QObject::connect(current, &QIODevice::readyRead, owner,
                         /** @brief 累计当前请求的响应字节并限制最大长度。 */
                         [this, current, currentGeneration] {
            if (!isCurrent(current, currentGeneration)) {
                return;
            }
            response.append(current->readAll());
            if (response.size() > GateHttpTransport::maxResponseBytes()) {
                finish(currentGeneration, GateHttpTerminal::ResponseTooLarge, true);
            }
        });
        QObject::connect(current, &QNetworkReply::finished, owner,
                         /** @brief 完成当前响应读取、校验及终态发布。 */
                         [this, current, currentGeneration] {
            if (!isCurrent(current, currentGeneration)) {
                return;
            }
            response.append(current->readAll());
            if (response.size() > GateHttpTransport::maxResponseBytes()) {
                finish(currentGeneration, GateHttpTerminal::ResponseTooLarge, false);
                return;
            }
            if (current->error() != QNetworkReply::NoError) {
                finish(currentGeneration, GateHttpTerminal::NetworkError, false);
                return;
            }

            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
            if (parseError.error != QJsonParseError::NoError || document.isNull()) {
                finish(currentGeneration, GateHttpTerminal::MalformedResponse, false);
                return;
            }
            finish(currentGeneration, GateHttpTerminal::Success, false);
        });
        QObject::connect(current, &QNetworkReply::errorOccurred, owner,
                         /** @brief 仅处理当前请求的网络错误。 */
                         [this, current, currentGeneration](QNetworkReply::NetworkError) {
            if (isCurrent(current, currentGeneration)) {
                finish(currentGeneration, GateHttpTerminal::NetworkError, false);
            }
        });
        deadline.start(request.deadlineMs);
    }

    /** @brief 仅取消匹配流程 ID 的当前 HTTP 操作。 */
    void cancel(quint64 flowId)
    {
        if (active && request.flowId == flowId) {
            finish(generation, GateHttpTerminal::Cancelled, true);
        }
    }

    /** @brief 结束当前操作并清空缓存，使迟到回调不再属于下一次请求。 */
    void reset()
    {
        if (active) {
            finish(generation, GateHttpTerminal::Cancelled, true);
        }
        ++generation;
        response.clear();
    }

    /** @brief 判断回调的网络对象和代号是否仍对应当前未结束操作。 */
    bool isCurrent(QNetworkReply *candidate, quint64 candidateGeneration) const
    {
        return active && candidateGeneration == generation && reply == candidate;
    }

    /** @brief 只完成匹配代号的当前操作，停止期限并根据调用参数取消网络 I/O。 */
    void finish(quint64 candidateGeneration, GateHttpTerminal terminal, bool abort)
    {
        if (!active || candidateGeneration != generation) {
            return;
        }

        const QByteArray body = terminal == GateHttpTerminal::Success ? response : QByteArray{};
        active = false;
        deadline.stop();
        QPointer<QNetworkReply> completedReply = reply;
        reply.clear();
        response.clear();

        if (completedReply) {
            QObject::disconnect(completedReply, nullptr, owner, nullptr);
            if (abort && !completedReply->isFinished()) {
                completedReply->abort();
            }
            completedReply->deleteLater();
        }

        emitResult(terminal, body);
    }

    /** @brief 解除网络对象与拥有者的连接，清空状态并安排网络对象销毁。 */
    void abandon(bool emitCancellation)
    {
        if (!active) {
            return;
        }
        if (emitCancellation) {
            finish(generation, GateHttpTerminal::Cancelled, true);
            return;
        }

        active = false;
        deadline.stop();
        QPointer<QNetworkReply> abandonedReply = reply;
        reply.clear();
        response.clear();
        if (abandonedReply) {
            QObject::disconnect(abandonedReply, nullptr, owner, nullptr);
            if (!abandonedReply->isFinished()) {
                abandonedReply->abort();
            }
            abandonedReply->deleteLater();
        }
    }

    /** @brief 通过外层对象发送请求的终态值，不暴露内部 reply 所有权。 */
    void emitResult(GateHttpTerminal terminal, const QByteArray &body)
    {
        GateHttpResult result;
        result.flowId = request.flowId;
        result.requestId = request.requestId;
        result.module = request.module;
        result.terminal = terminal;
        result.body = body;
        emit owner->finished(result);
    }

    GateHttpTransport *owner;
    QNetworkAccessManager manager;
    QTimer deadline;
    QPointer<QNetworkReply> reply;
    GateHttpRequest request;
    QByteArray response;
    quint64 generation = 0;
    bool active = false;
};

GateHttpTransport::GateHttpTransport(QObject *parent)
    : QObject(parent), _impl(std::make_unique<Impl>(this))
{
}

GateHttpTransport::~GateHttpTransport() = default;

void GateHttpTransport::post(const GateHttpRequest &request)
{
    _impl->post(request);
}

void GateHttpTransport::cancel(quint64 flowId)
{
    _impl->cancel(flowId);
}

void GateHttpTransport::reset()
{
    _impl->reset();
}
