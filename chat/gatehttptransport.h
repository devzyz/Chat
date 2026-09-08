#ifndef GATEHTTPTRANSPORT_H
#define GATEHTTPTRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QUrl>
#include <QtGlobal>

#include <memory>

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

struct GateHttpResult
{
    quint64 flowId = 0;
    int requestId = -1;
    int module = -1;
    GateHttpTerminal terminal = GateHttpTerminal::NetworkError;
    QByteArray body;
};

class GateHttpTransport final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(GateHttpTransport)

public:
    static constexpr qsizetype MaxResponseBytes() noexcept { return 8192; }

    explicit GateHttpTransport(QObject *parent = nullptr);
    ~GateHttpTransport() override;

    void post(const GateHttpRequest &request);
    void cancel(quint64 flowId);
    void reset();

signals:
    void finished(const GateHttpResult &result);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // GATEHTTPTRANSPORT_H
