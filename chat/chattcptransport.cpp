#include "chattcptransport.h"

#include "tcpframedecoder.h"

#include <QDataStream>
#include <QNetworkProxy>
#include <QPointer>
#include <QQueue>
#include <QTcpSocket>
#include <QTimer>

struct ChatTcpTransport::Impl
{
    explicit Impl(ChatTcpTransport *owner)
        : owner(owner)
    {
        connectDeadline.setSingleShot(true);
        writeDeadline.setSingleShot(true);
        QObject::connect(&connectDeadline, &QTimer::timeout, owner, [this] {
            finish(generation, ChatTcpTerminal::ConnectDeadlineExceeded, true);
        });
        QObject::connect(&writeDeadline, &QTimer::timeout, owner, [this] {
            finish(generation, ChatTcpTerminal::WriteDeadlineExceeded, true);
        });
    }

    ~Impl()
    {
        abandon();
    }

    quint64 connectTo(const ChatTcpEndpoint &next)
    {
        if (active) {
            finish(generation, ChatTcpTerminal::Superseded, true);
        }

        ++generation;
        endpoint = next;
        decoder.reset();
        writes.clear();
        currentWriteBytes = 0;
        connected = false;
        active = true;

        if (endpoint.host.isEmpty() || endpoint.port == 0 || endpoint.flowId == 0
            || endpoint.connectDeadlineMs <= 0 || endpoint.writeDeadlineMs <= 0) {
            finish(generation, ChatTcpTerminal::Refused, false);
            return generation;
        }

        auto *nextSocket = new QTcpSocket(owner);
        nextSocket->setProxy(QNetworkProxy::NoProxy);
        socket = nextSocket;
        const quint64 currentGeneration = generation;

        QObject::connect(nextSocket, &QTcpSocket::connected, owner,
                         [this, nextSocket, currentGeneration] {
            if (!isCurrent(nextSocket, currentGeneration)) {
                return;
            }
            connected = true;
            connectDeadline.stop();
            emit owner->connected(currentGeneration, endpoint.flowId);
        });
        QObject::connect(nextSocket, &QTcpSocket::readyRead, owner,
                         [this, nextSocket, currentGeneration] {
            if (!isCurrent(nextSocket, currentGeneration)) {
                return;
            }
            const QVector<DecodedTcpFrame> decoded = decoder.append(nextSocket->readAll());
            if (decoder.hasError()) {
                finish(currentGeneration, ChatTcpTerminal::ProtocolError, true);
                return;
            }
            for (const DecodedTcpFrame &value : decoded) {
                ChatTcpFrame frame;
                frame.generation = currentGeneration;
                frame.flowId = endpoint.flowId;
                frame.messageId = value.messageId;
                frame.body = value.body;
                emit owner->frameReceived(frame);
            }
        });
        QObject::connect(nextSocket, &QTcpSocket::bytesWritten, owner,
                         [this, nextSocket, currentGeneration](qint64 count) {
            if (!isCurrent(nextSocket, currentGeneration) || currentWriteBytes <= 0) {
                return;
            }
            currentWriteBytes -= count;
            if (currentWriteBytes > 0) {
                writeDeadline.start(endpoint.writeDeadlineMs);
                return;
            }
            writes.dequeue();
            currentWriteBytes = 0;
            startNextWrite();
        });
        QObject::connect(nextSocket, &QTcpSocket::errorOccurred, owner,
                         [this, nextSocket, currentGeneration](QAbstractSocket::SocketError error) {
            if (!isCurrent(nextSocket, currentGeneration)) {
                return;
            }
            const ChatTcpTerminal terminal =
                !connected && error == QAbstractSocket::ConnectionRefusedError
                    ? ChatTcpTerminal::Refused
                    : (connected ? ChatTcpTerminal::PeerClosed : ChatTcpTerminal::Refused);
            finish(currentGeneration, terminal, false);
        });
        QObject::connect(nextSocket, &QTcpSocket::disconnected, owner,
                         [this, nextSocket, currentGeneration] {
            if (isCurrent(nextSocket, currentGeneration)) {
                finish(currentGeneration, ChatTcpTerminal::PeerClosed, false);
            }
        });

        connectDeadline.start(endpoint.connectDeadlineMs);
        nextSocket->connectToHost(endpoint.host, endpoint.port);
        return generation;
    }

    bool send(quint16 messageId, const QByteArray &body)
    {
        if (!active || !connected || !socket || body.size() > ChatTcpTransport::MaxBodyBytes()) {
            return false;
        }

        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << messageId << static_cast<quint16>(body.size());
        bytes.append(body);
        writes.enqueue(std::move(bytes));
        startNextWrite();
        return true;
    }

    void close()
    {
        if (active) {
            finish(generation, ChatTcpTerminal::LocalClosed, true);
        }
    }

    void reset()
    {
        if (active) {
            finish(generation, ChatTcpTerminal::Reset, true);
        } else {
            ++generation;
            decoder.reset();
            writes.clear();
        }
    }

    bool isCurrent(QTcpSocket *candidate, quint64 candidateGeneration) const
    {
        return active && candidateGeneration == generation && socket == candidate;
    }

    void startNextWrite()
    {
        if (!active || !connected || !socket || currentWriteBytes > 0) {
            return;
        }
        if (writes.isEmpty()) {
            writeDeadline.stop();
            return;
        }

        currentWriteBytes = socket->write(writes.head());
        if (currentWriteBytes < 0) {
            finish(generation, ChatTcpTerminal::PeerClosed, false);
            return;
        }
        if (currentWriteBytes == 0) {
            finish(generation, ChatTcpTerminal::WriteDeadlineExceeded, true);
            return;
        }
        writeDeadline.start(endpoint.writeDeadlineMs);
    }

    void finish(quint64 candidateGeneration, ChatTcpTerminal terminal, bool abort)
    {
        if (!active || candidateGeneration != generation) {
            return;
        }

        const ChatTcpOutcome outcome{generation, endpoint.flowId, terminal};
        active = false;
        connected = false;
        connectDeadline.stop();
        writeDeadline.stop();
        decoder.reset();
        writes.clear();
        currentWriteBytes = 0;

        QPointer<QTcpSocket> completed = socket;
        socket.clear();
        if (completed) {
            QObject::disconnect(completed, nullptr, owner, nullptr);
            if (abort && completed->state() != QAbstractSocket::UnconnectedState) {
                completed->abort();
            }
            completed->deleteLater();
        }
        emit owner->finished(outcome);
    }

    void abandon()
    {
        active = false;
        connectDeadline.stop();
        writeDeadline.stop();
        decoder.reset();
        writes.clear();
        QTcpSocket *abandoned = socket.data();
        socket.clear();
        if (abandoned) {
            QObject::disconnect(abandoned, nullptr, owner, nullptr);
            abandoned->abort();
            delete abandoned;
        }
    }

    ChatTcpTransport *owner;
    QTimer connectDeadline;
    QTimer writeDeadline;
    QPointer<QTcpSocket> socket;
    TcpFrameDecoder decoder;
    QQueue<QByteArray> writes;
    ChatTcpEndpoint endpoint;
    quint64 generation = 0;
    qint64 currentWriteBytes = 0;
    bool connected = false;
    bool active = false;
};

ChatTcpTransport::ChatTcpTransport(QObject *parent)
    : QObject(parent), _impl(std::make_unique<Impl>(this))
{
}

ChatTcpTransport::~ChatTcpTransport() = default;

quint64 ChatTcpTransport::connectTo(const ChatTcpEndpoint &endpoint)
{
    return _impl->connectTo(endpoint);
}

bool ChatTcpTransport::send(quint16 messageId, const QByteArray &body)
{
    return _impl->send(messageId, body);
}

void ChatTcpTransport::close()
{
    _impl->close();
}

void ChatTcpTransport::reset()
{
    _impl->reset();
}
