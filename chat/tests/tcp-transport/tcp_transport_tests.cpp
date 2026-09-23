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

/** 按大端协议编码消息编号、正文长度及正文。 */
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

/** 持有真实回环 TCP 监听器与连接，提供限速及断连测试控制。 */
class LoopbackTcpPeer final : public QObject
{
public:
    /** 安装新连接接收和销毁清理回调。 */
    explicit LoopbackTcpPeer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&_server, &QTcpServer::newConnection, this, /** 接收并跟踪所有待处理连接。 */ [this] {
            while (QTcpSocket *socket = _server.nextPendingConnection()) {
                _sockets.append(socket);
                connect(socket, &QObject::destroyed, this,
                        /** 套接字销毁时移除跟踪引用。 */ [this, socket] { _sockets.removeAll(socket); });
            }
        });
    }

    /** 在随机回环端口监听并返回绑定结果。 */
    bool listen()
    {
        return _server.listen(QHostAddress::LocalHost, 0);
    }

    /** 返回实际监听端口。 */
    quint16 port() const
    {
        return _server.serverPort();
    }

    /** 返回当前跟踪的连接条数。 */
    int connectionCount() const
    {
        return _sockets.size();
    }

    /** 统计仍未断开的连接。 */
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

    /** 按索引借用被跟踪套接字，越界或已销毁时返回空。 */
    QTcpSocket *socket(int index) const
    {
        return index >= 0 && index < _sockets.size() ? _sockets[index].data() : nullptr;
    }

    /** 限制接收缓冲为一个字节以制造写入背压。 */
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

/** 构建带连接及写入期限的回环端点与流程标识。 */
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

/** 临时绑定随机回环端口再释放，提供预期无人监听的测试端口。 */
quint16 unusedLoopbackPort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return reservation.serverPort();
}

} // namespace

/** 验证真实 TCP 的编解码、代次隔离、有界失败及资源释放。 */
class TcpTransportTests final : public QObject
{
    Q_OBJECT

private slots:
    /** 验证 TCP 连接成功保持连接代次及认证流程标识。 */
    void connectPreservesGenerationAndFlowIdentity();
    /** 验证发送字节与生产帧格式一致。 */
    void sendWritesProductionFrame();
    /** 验证分片帧只在完整到达后解码一次。 */
    void fragmentedFrameDecodedOnce();
    /** 验证多个粘连帧按顺序解码。 */
    void coalescedFramesStayOrdered();
    /** 验证正文达到最大长度时仍可接收。 */
    void maximumFrameIsAccepted();
    /** 验证超长帧终止连接并报告协议错误。 */
    void malformedOversizedFrameTerminates();
    /** 验证连接拒绝在期限内只产生一个终态。 */
    void refusedConnectHasOneBoundedOutcome();
    /** 验证不消费数据的对端触发写期限并关闭连接。 */
    void writeDeadlineAbortsSilentPeer();
    /** 验证写入中对端关闭只产生一个终态。 */
    void peerCloseMidWriteHasOneTerminalOutcome();
    /** 验证重置丢弃半帧，新连接数据不与旧缓冲拼接。 */
    void resetDiscardsHalfFrame();
    /** 验证旧连接代次的迟到结果不完成新连接尝试。 */
    void lateOldGenerationCannotCompleteRetry();
    /** 验证主动关闭及销毁释放所有传输资源。 */
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
            /** 记录成功连接的代次及流程标识。 */ [&connections](quint64 generation, quint64 flowId) {
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
            /** 收集完整解码帧以断言内容、顺序及代次。 */ [&frames](const ChatTcpFrame &value) { frames.append(value); });
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
            /** 收集完整解码帧以断言内容、顺序及代次。 */ [&frames](const ChatTcpFrame &value) { frames.append(value); });
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
            /** 收集完整解码帧以断言内容、顺序及代次。 */ [&frames](const ChatTcpFrame &value) { frames.append(value); });
    transport.connectTo(endpoint(peer.port(), 405));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.socket(0)->write(frame(1010, QByteArray(ChatTcpTransport::maxBodyBytes(), 'x')));
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 1000);
    QCOMPARE(frames[0].body.size(), ChatTcpTransport::maxBodyBytes());
}

void TcpTransportTests::malformedOversizedFrameTerminates()
{
    // Q04-TCP-06
    LoopbackTcpPeer peer;
    QVERIFY(peer.listen());
    ChatTcpTransport transport;
    QList<ChatTcpOutcome> outcomes;
    connect(&transport, &ChatTcpTransport::finished, this,
            /** 收集传输终态以断言错误分类和完成次数。 */ [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport.connectTo(endpoint(peer.port(), 406));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.socket(0)->write(frame(1011, QByteArray(ChatTcpTransport::maxBodyBytes() + 1, 'x')));
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
            /** 收集传输终态以断言错误分类和完成次数。 */ [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
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
            /** 收集传输终态以断言错误分类和完成次数。 */ [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport.connectTo(endpoint(peer.port(), 408, 500, 20));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    peer.throttleReads(0);
    const QByteArray body(ChatTcpTransport::maxBodyBytes(), 'x');
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
            /** 收集传输终态以断言错误分类和完成次数。 */ [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    transport.connectTo(endpoint(peer.port(), 409, 500, 500));
    QTRY_COMPARE_WITH_TIMEOUT(peer.connectionCount(), 1, 1000);
    const QByteArray body(ChatTcpTransport::maxBodyBytes(), 'x');
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
            /** 收集完整解码帧以断言内容、顺序及代次。 */ [&frames](const ChatTcpFrame &value) { frames.append(value); });
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
            /** 收集旧代与新代的终态，供隔离断言。 */ [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
    connect(&transport, &ChatTcpTransport::connected, this,
            /** 记录成功连接的代次及流程标识。 */ [&connections](quint64 generation, quint64 flowId) {
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
            /** 收集传输终态以断言错误分类和完成次数。 */ [&outcomes](const ChatTcpOutcome &value) { outcomes.append(value); });
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
