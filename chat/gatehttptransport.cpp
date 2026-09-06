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

struct GateHttpTransport::Impl
{
    explicit Impl(GateHttpTransport *owner)
        : owner(owner)
    {
        manager.setProxy(QNetworkProxy::NoProxy);
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, owner, [this] {
            finish(generation, GateHttpTerminal::DeadlineExceeded, true);
        });
    }

    ~Impl()
    {
        abandon(false);
    }

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
                         [this, current, currentGeneration] {
            if (!isCurrent(current, currentGeneration)) {
                return;
            }
            response.append(current->readAll());
            if (response.size() > GateHttpTransport::MaxResponseBytes()) {
                finish(currentGeneration, GateHttpTerminal::ResponseTooLarge, true);
            }
        });
        QObject::connect(current, &QNetworkReply::finished, owner,
                         [this, current, currentGeneration] {
            if (!isCurrent(current, currentGeneration)) {
                return;
            }
            response.append(current->readAll());
            if (response.size() > GateHttpTransport::MaxResponseBytes()) {
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
                         [this, current, currentGeneration](QNetworkReply::NetworkError) {
            if (isCurrent(current, currentGeneration)) {
                finish(currentGeneration, GateHttpTerminal::NetworkError, false);
            }
        });
        deadline.start(request.deadlineMs);
    }

    void cancel(quint64 flowId)
    {
        if (active && request.flowId == flowId) {
            finish(generation, GateHttpTerminal::Cancelled, true);
        }
    }

    void reset()
    {
        if (active) {
            finish(generation, GateHttpTerminal::Cancelled, true);
        }
        ++generation;
        response.clear();
    }

    bool isCurrent(QNetworkReply *candidate, quint64 candidateGeneration) const
    {
        return active && candidateGeneration == generation && reply == candidate;
    }

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
