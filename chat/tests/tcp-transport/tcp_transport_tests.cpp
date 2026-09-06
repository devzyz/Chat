#include "chattcptransport.h"

#include <QDataStream>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <memory>

namespace {

QByteArray frame(quint16 messageId, const QByteArray &body)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << messageId << static_cast<quint16>(body.size());
    bytes.append(body);
    return bytes;
}

class LoopbackTcpPeer final : public QObject
{
public:
    explicit LoopbackTcpPeer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = _server.nextPendingConnection()) {
                _sockets.append(socket);
                connect(socket, &QObject::destroyed, this,
                        [this, socket] { _sockets.removeAll(socket); });
            }
        });
    }

    bool listen()
    {
        return _server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return _server.serverPort();
    }

    int connectionCount() const
    {
        return _sockets.size();
    }

    int activeConnections() const
    {
        int count = 0;
        for (const QPointer<QTcpSocket> &socket : _sockets) {
            if (socket && socket->state() != QAbstractSocket::UnconnectedState) {
                ++count;
            }
        }
        return count;
    }

    QTcpSocket *socket(int index) const
    {
        return index >= 0 && index < _sockets.size() ? _sockets[index].data() : nullptr;
    }

    void throttleReads(int index)
    {
        if (QTcpSocket *peerSocket = socket(index)) {
            peerSocket->setReadBufferSize(1);
        }
    }

private:
    QTcpServer _server;
    QList<QPointer<QTcpSocket>> _sockets;
};

ChatTcpEndpoint endpoint(quint16 port, quint64 flowId,
                         int connectDeadlineMs = 500,
                         int writeDeadlineMs = 500)
{
    ChatTcpEndpoint value;
    value.host = QStringLiteral("127.0.0.1");
    value.port = port;
    value.flowId = flowId;
    value.connectDeadlineMs = connectDeadlineMs;
    value.writeDeadlineMs = writeDeadlineMs;
    return value;
}

quint16 unusedLoopbackPort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return reservation.serverPort();
}

} // namespace

class TcpTransportTests final : public QObject
{
    Q_OBJECT

private slots:
    void connectPreservesGenerationAndFlowIdentity();
    void sendWritesProductionFrame();
    void fragmentedFrameDecodedOnce();
    void coalescedFramesStayOrdered();
    void maximumFrameIsAccepted();
    void malformedOversizedFrameTerminates();
    void refusedConnectHasOneBoundedOutcome();
    void writeDeadlineAbortsSilentPeer();
    void peerCloseMidWriteHasOneTerminalOutcome();
    void resetDiscardsHalfFrame();
    void lateOldGenerationCannotCompleteRetry();
    void closeAndDeleteReleaseOwnedResources();
};

void TcpTransportTests::connectPreservesGenerationAndFlowIdentity()
{
    // Q04-TCP-01
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<QPair<quint64, quint64>> connections;
    connect(&transport, &ChatTcpTransport::connected, this,
            [&connections](quint64 generation, quint64 flowId) {
                connections.append({generation, flowId});
            });

    const quint64 generation = transport.connectTo(endpoint(peer.port(), 401));
    QTRY_COMPARE_WITH_TIMEOUT(connections.size(), 1, 1000);
    QCOMPARE(connections[0].first, generation);
    QCOMPARE(connections[0].second, quint64(401));
}

void TcpTransportTests::sendWritesProductionFrame()
{
    // Q04-TCP-02
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    transport.connectTo(endpoint(peer.port(), 402));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);

    QVERIFY(transport.send(1006, QByteArrayLiteral("hello")));
    QTcpSocket *socket = peer.socket(0);
    QVERIFY(socket);
    QTRY_COMPARE_WITH_TIMEOUT(socket->bytesAvailable(), qint64(9), 1000);
    QCOMPARE(socket->readAll(), frame(1006, QByteArrayLiteral("hello")));
}

void TcpTransportTests::fragmentedFrameDecodedOnce()
{
    // Q04-TCP-03
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpFrame> frames;
    connect(&transport, &ChatTcpTransport::frameReceived, this,
            [&frames](const ChatTcpFrame &value) { frames.append(value); });
    const quint64 generation = transport.connectTo(endpoint(peer.port(), 403));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    const QByteArray bytes = frame(1007, QByteArrayLiteral("fragmented"));
    peer.socket(0)->write(bytes.left(3));
    QTest::qWait(20);
    QVERIFY(frames.isEmpty());
    peer.socket(0)->write(bytes.mid(3));
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 1000);
    QCOMPARE(frames[0].generation, generation);
    QCOMPARE(frames[0].flowId, quint64(403));
    QCOMPARE(frames[0].body, QByteArrayLiteral("fragmented"));
}

void TcpTransportTests::coalescedFramesStayOrdered()
{
    // Q04-TCP-04
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpFrame> frames;
    connect(&transport, &ChatTcpTransport::frameReceived, this,
            [&frames](const ChatTcpFrame &value) { frames.append(value); });
    transport.connectTo(endpoint(peer.port(), 404));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.socket(0)->write(frame(1008, QByteArrayLiteral("one"))
                          + frame(1009, QByteArrayLiteral("two")));
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 2, 1000);
    QCOMPARE(frames[0].messageId, quint16(1008));
    QCOMPARE(frames[1].messageId, quint16(1009));
}

void TcpTransportTests::maximumFrameIsAccepted()
{
    // Q04-TCP-05
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpFrame> frames;
    connect(&transport, &ChatTcpTransport::frameReceived, this,
            [&frames](const ChatTcpFrame &value) { frames.append(value); });
    transport.connectTo(endpoint(peer.port(), 405));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.socket(0)->write(frame(1010, QByteArray(ChatTcpTransport::MaxBodyBytes(), 'x')));
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 1000);
    QCOMPARE(frames[0].body.size(), ChatTcpTransport::MaxBodyBytes());
}

void TcpTransportTests::malformedOversizedFrameTerminates()
{
    // Q04-TCP-06
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpOutcome> outcomes;
    connect(&transport, &ChatTcpTransport::finished, this,
            [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport.connectTo(endpoint(peer.port(), 406));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.socket(0)->write(frame(1011, QByteArray(ChatTcpTransport::MaxBodyBytes() + 1, 'x')));
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, 1000);
    QCOMPARE(outcomes[0].terminal, ChatTcpTerminal::ProtocolError);
    QCOMPARE(outcomes[0].flowId, quint64(406));
}

void TcpTransportTests::refusedConnectHasOneBoundedOutcome()
{
    // Q04-TCP-07
    const quint16 port = unusedLoopbackPort();
    QVERIFY(port != 0);
    ChatTcpTransport transport;
    QList<ChatTcpOutcome> outcomes;
    connect(&transport, &ChatTcpTransport::finished, this,
            [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    const quint64 generation = transport.connectTo(endpoint(port, 407, 1000));
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, 2000);
    QCOMPARE(outcomes[0].generation, generation);
    QCOMPARE(outcomes[0].flowId, quint64(407));
    QVERIFY(outcomes[0].terminal == ChatTcpTerminal::Refused
            || outcomes[0].terminal == ChatTcpTerminal::ConnectDeadlineExceeded);
    QTest::qWait(100);
    QCOMPARE(outcomes.size(), 1);
}

void TcpTransportTests::writeDeadlineAbortsSilentPeer()
{
    // Q04-TCP-08
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpOutcome> outcomes;
    connect(&transport, &ChatTcpTransport::finished, this,
            [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport.connectTo(endpoint(peer.port(), 408, 500, 20));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.throttleReads(0);
    const QByteArray body(ChatTcpTransport::MaxBodyBytes(), 'x');
    for (int index = 0; index < 8192; ++index) {
        QVERIFY(transport.send(1012, body));
    }
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, 3000);
    QCOMPARE(outcomes[0].terminal, ChatTcpTerminal::WriteDeadlineExceeded);
}

void TcpTransportTests::peerCloseMidWriteHasOneTerminalOutcome()
{
    // Q04-TCP-09
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpOutcome> outcomes;
    connect(&transport, &ChatTcpTransport::finished, this,
            [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport.connectTo(endpoint(peer.port(), 409, 500, 500));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    const QByteArray body(ChatTcpTransport::MaxBodyBytes(), 'x');
    for (int index = 0; index < 2048; ++index) {
        QVERIFY(transport.send(1013, body));
    }
    peer.socket(0)->abort();
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, 2000);
    QCOMPARE(outcomes[0].terminal, ChatTcpTerminal::PeerClosed);
    QTest::qWait(100);
    QCOMPARE(outcomes.size(), 1);
}

void TcpTransportTests::resetDiscardsHalfFrame()
{
    // Q04-TCP-10
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpFrame> frames;
    connect(&transport, &ChatTcpTransport::frameReceived, this,
            [&frames](const ChatTcpFrame &value) { frames.append(value); });
    transport.connectTo(endpoint(peer.port(), 410));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    const QByteArray stale = frame(1014, QByteArrayLiteral("stale"));
    peer.socket(0)->write(stale.left(6));
    QTest::qWait(20);
    transport.reset();
    transport.connectTo(endpoint(peer.port(), 411));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 2, 1000);
    peer.socket(1)->write(frame(1015, QByteArrayLiteral("fresh")));
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 1000);
    QCOMPARE(frames[0].flowId, quint64(411));
    QCOMPARE(frames[0].body, QByteArrayLiteral("fresh"));
}

void TcpTransportTests::lateOldGenerationCannotCompleteRetry()
{
    // Q04-TCP-11
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    const quint16 refusedPort = unusedLoopbackPort();
    QVERIFY(refusedPort != 0);
    ChatTcpTransport transport;
    QList<ChatTcpOutcome> outcomes;
    QList<QPair<quint64, quint64>> connections;
    connect(&transport, &ChatTcpTransport::finished, this,
            [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    connect(&transport, &ChatTcpTransport::connected, this,
            [&connections](quint64 generation, quint64 flowId) {
                connections.append({generation, flowId});
            });
    const quint64 oldGeneration = transport.connectTo(endpoint(refusedPort, 412));
    const quint64 newGeneration = transport.connectTo(endpoint(peer.port(), 413));
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, 1000);
    QCOMPARE(outcomes[0].generation, oldGeneration);
    QCOMPARE(outcomes[0].flowId, quint64(412));
    QCOMPARE(outcomes[0].terminal, ChatTcpTerminal::Superseded);
    QTRY_COMPARE_WITH_TIMEOUT(connections.size(), 1, 1000);
    QCOMPARE(connections[0], qMakePair(newGeneration, quint64(413)));
    QTest::qWait(100);
    QCOMPARE(outcomes.size(), 1);
}

void TcpTransportTests::closeAndDeleteReleaseOwnedResources()
{
    // Q04-TCP-12
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    auto transport = std::make_unique<ChatTcpTransport>();
    QList<ChatTcpOutcome> outcomes;
    connect(transport.get(), &ChatTcpTransport::finished, this,
            [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport->connectTo(endpoint(peer.port(), 414));
    QTRY_COMPARE_WITH_TIMEOUT(peer.activeConnections(), 1, 1000);
    transport->close();
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, 1000);
    QCOMPARE(outcomes[0].terminal, ChatTcpTerminal::LocalClosed);
    QTRY_COMPARE_WITH_TIMEOUT(peer.activeConnections(), 0, 1000);
    transport.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(peer.activeConnections(), 0);
}

QTEST_GUILESS_MAIN(TcpTransportTests)

#include "tcp_transport_tests.moc"
