#ifndef CHATTCPTRANSPORT_H
#define CHATTCPTRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <memory>

struct ChatTcpEndpoint
{
    QString host;
    quint16 port = 0;
    quint64 flowId = 0;
    int connectDeadlineMs = 0;
    int writeDeadlineMs = 0;
};

enum class ChatTcpTerminal
{
    Refused,
    ConnectDeadlineExceeded,
    WriteDeadlineExceeded,
    PeerClosed,
    LocalClosed,
    Reset,
    Superseded,
    ProtocolError
};

struct ChatTcpFrame
{
    quint64 generation = 0;
    quint64 flowId = 0;
    quint16 messageId = 0;
    QByteArray body;
};

struct ChatTcpOutcome
{
    quint64 generation = 0;
    quint64 flowId = 0;
    ChatTcpTerminal terminal = ChatTcpTerminal::Refused;
};

class ChatTcpTransport final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ChatTcpTransport)

public:
    static constexpr qsizetype MaxBodyBytes() noexcept { return 2048; }

    explicit ChatTcpTransport(QObject *parent = nullptr);
    ~ChatTcpTransport() override;

    quint64 connectTo(const ChatTcpEndpoint &endpoint);
    bool send(quint16 messageId, const QByteArray &body);
    void close();
    void reset();

signals:
    void connected(quint64 generation, quint64 flowId);
    void frameReceived(const ChatTcpFrame &frame);
    void finished(const ChatTcpOutcome &outcome);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // CHATTCPTRANSPORT_H
