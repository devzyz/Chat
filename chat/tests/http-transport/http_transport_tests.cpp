#include "gatehttptransport.h"

#include <QCoreApplication>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace {

class LoopbackHttpPeer final : public QObject
{
public:
    enum class Behavior { Respond, Hold, ResetOnAccept, CloseMidResponse };

    struct Plan {
        QByteArray body = QByteArrayLiteral("{\"ok\":true}");
        Behavior behavior = Behavior::Respond;
        int delayMs = 0;
    };

    explicit LoopbackHttpPeer(Behavior connectionBehavior = Behavior::Respond,
                              QObject *parent = nullptr)
        : QObject(parent), _connectionBehavior(connectionBehavior)
    {
        connect(&_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = _server.nextPendingConnection()) {
                _sockets.append(socket);
                if (_connectionBehavior == Behavior::ResetOnAccept) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("request-handled").toBool()) {
                        socket->readAll();
                        return;
                    }
                    _requests[socket].append(socket->readAll());
                    const QByteArray request = _requests.value(socket);
                    const qsizetype firstSpace = request.indexOf(' ');
                    const qsizetype secondSpace = request.indexOf(' ', firstSpace + 1);
                    if (firstSpace < 0 || secondSpace < 0 || !request.contains("\r\n\r\n")) {
                        return;
                    }
                    const QString path = QString::fromLatin1(
                        request.mid(firstSpace + 1, secondSpace - firstSpace - 1));
                    const Plan plan = _plans.value(path);
                    socket->setProperty("request-handled", true);
                    if (plan.behavior == Behavior::Hold) {
                        return;
                    }
                    if (plan.behavior == Behavior::CloseMidResponse) {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                      "Content-Length: 24\r\nConnection: close\r\n\r\n{\"partial\":");
                        socket->flush();
                        socket->disconnectFromHost();
                        return;
                    }
                    const auto respond = [socket, body = plan.body] {
                        if (!socket || socket->state() == QAbstractSocket::UnconnectedState) {
                            return;
                        }
                        QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
                        response += QByteArray::number(body.size());
                        response += "\r\nConnection: close\r\n\r\n";
                        response += body;
                        socket->write(response);
                        socket->disconnectFromHost();
                    };
                    if (plan.delayMs > 0) {
                        QTimer::singleShot(plan.delayMs, socket, respond);
                    } else {
                        respond();
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QObject::destroyed, this, [this, socket] {
                    _requests.remove(socket);
                    _sockets.removeAll(socket);
                });
            }
        });
    }

    bool listen()
    {
        return _server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(_server.serverPort())
                        .arg(path));
    }

    void setPlan(const QString &path, Plan plan)
    {
        _plans.insert(path, std::move(plan));
    }

    int activeConnections() const
    {
        int active = 0;
        for (const QPointer<QTcpSocket> &socket : _sockets) {
            if (socket && socket->state() != QAbstractSocket::UnconnectedState) {
                ++active;
            }
        }
        return active;
    }

private:
    QTcpServer _server;
    Behavior _connectionBehavior;
    QHash<QString, Plan> _plans;
    QHash<QTcpSocket *, QByteArray> _requests;
    QList<QPointer<QTcpSocket>> _sockets;
};

GateHttpRequest request(const QUrl &url, quint64 flowId, int deadlineMs = 1000)
{
    GateHttpRequest value;
    value.url = url;
    value.body = QByteArrayLiteral("{}");
    value.flowId = flowId;
    value.requestId = 1004;
    value.module = 2;
    value.deadlineMs = deadlineMs;
    return value;
}

QByteArray jsonBody(qsizetype size)
{
    const QByteArray prefix = "{\"value\":\"";
    const QByteArray suffix = "\"}";
    return prefix + QByteArray(size - prefix.size() - suffix.size(), 'x') + suffix;
}

} // namespace

class HttpTransportTests final : public QObject
{
    Q_OBJECT

private slots:
    void successPreservesRequestAndFlowIdentity();
    void refusedConnectionHasOneBoundedOutcome();
    void finiteDeadlineAbortsAnUnresponsivePeer();
    void malformedJsonIsRejectedWithoutLeakingItsBody();
    void maximumResponseIsAccepted();
    void oneByteOverMaximumIsRejected();
    void peerClosingMidResponseHasOneNetworkOutcome();
    void explicitCancelHasExactlyOneTerminalOutcome();
    void lateReplyFromAnOldFlowCannotCompleteTheNewFlow();
    void deletingTransportReleasesReplyAndLoopbackSocket();
};

void HttpTransportTests::successPreservesRequestAndFlowIdentity()
{
    // Q04-HTTP-01
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/success")), 41));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::Success);
    QCOMPARE(results[0].flowId, quint64(41));
    QCOMPARE(results[0].requestId, 1004);
    QCOMPARE(results[0].module, 2);
    QCOMPARE(results[0].body, QByteArrayLiteral("{\"ok\":true}"));
}

void HttpTransportTests::refusedConnectionHasOneBoundedOutcome()
{
    // Q04-HTTP-02
    LoopbackHttpPeer peer(LoopbackHttpPeer::Behavior::ResetOnAccept);
    QVERIFY(peer.listen());

    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });
    transport.post(request(peer.url(QStringLiteral("/refused")), 42));

    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::NetworkError);
    QTest::qWait(50);
    QCOMPARE(results.size(), 1);
}

void HttpTransportTests::finiteDeadlineAbortsAnUnresponsivePeer()
{
    // Q04-HTTP-03
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/hold"), {{}, LoopbackHttpPeer::Behavior::Hold, 0});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/hold")), 43, 100));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 1000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::DeadlineExceeded);
}

void HttpTransportTests::malformedJsonIsRejectedWithoutLeakingItsBody()
{
    // Q04-HTTP-04
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/malformed"), {QByteArrayLiteral("not-json")});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/malformed")), 44));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::MalformedResponse);
    QVERIFY(results[0].body.isEmpty());
}

void HttpTransportTests::maximumResponseIsAccepted()
{
    // Q04-HTTP-05
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/maximum"), {jsonBody(GateHttpTransport::MaxResponseBytes())});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/maximum")), 45));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::Success);
    QCOMPARE(results[0].body.size(), GateHttpTransport::MaxResponseBytes());
}

void HttpTransportTests::oneByteOverMaximumIsRejected()
{
    // Q04-HTTP-06
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/oversized"), {jsonBody(GateHttpTransport::MaxResponseBytes() + 1)});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/oversized")), 46));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::ResponseTooLarge);
    QVERIFY(results[0].body.isEmpty());
}

void HttpTransportTests::peerClosingMidResponseHasOneNetworkOutcome()
{
    // Q04-HTTP-07
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/partial"), {{}, LoopbackHttpPeer::Behavior::CloseMidResponse, 0});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/partial")), 47));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::NetworkError);
    QTest::qWait(50);
    QCOMPARE(results.size(), 1);
}

void HttpTransportTests::explicitCancelHasExactlyOneTerminalOutcome()
{
    // Q04-HTTP-08
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/cancel"), {{}, LoopbackHttpPeer::Behavior::Hold, 0});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/cancel")), 48));
    transport.cancel(48);
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 1000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::Cancelled);
    QTest::qWait(100);
    QCOMPARE(results.size(), 1);
}

void HttpTransportTests::lateReplyFromAnOldFlowCannotCompleteTheNewFlow()
{
    // Q04-HTTP-09
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/old"), {QByteArrayLiteral("{\"old\":true}"),
                                           LoopbackHttpPeer::Behavior::Respond, 250});
    peer.setPlan(QStringLiteral("/current"), {QByteArrayLiteral("{\"current\":true}")});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/old")), 49));
    QTest::qWait(20);
    transport.post(request(peer.url(QStringLiteral("/current")), 50));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 2000);
    QCOMPARE(results[0].flowId, quint64(49));
    QCOMPARE(results[0].terminal, GateHttpTerminal::Cancelled);
    QCOMPARE(results[1].flowId, quint64(50));
    QCOMPARE(results[1].terminal, GateHttpTerminal::Success);
    QTest::qWait(300);
    QCOMPARE(results.size(), 2);
}

void HttpTransportTests::deletingTransportReleasesReplyAndLoopbackSocket()
{
    // Q04-HTTP-10
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/cleanup"), {{}, LoopbackHttpPeer::Behavior::Hold, 0});
    auto transport = std::make_unique<GateHttpTransport>();
    transport->post(request(peer.url(QStringLiteral("/cleanup")), 51));
    QTRY_COMPARE_WITH_TIMEOUT(peer.activeConnections(), 1, 1000);
    transport.reset();
    QTRY_COMPARE_WITH_TIMEOUT(peer.activeConnections(), 0, 1000);
}

QTEST_GUILESS_MAIN(HttpTransportTests)

#include "http_transport_tests.moc"
