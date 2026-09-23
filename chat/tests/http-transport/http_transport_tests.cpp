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

/** 提供真实回环 HTTP 对端，按路径注入延时、挂起、拒绝和截断响应。 */
class LoopbackHttpPeer final : public QObject
{
public:
    enum class Behavior { Respond, Hold, ResetOnAccept, CloseMidResponse };

    /** 描述单路径响应正文、故障模式和延迟。 */
    struct Plan {
        QByteArray body = QByteArrayLiteral("{\"ok\":true}");
        Behavior behavior = Behavior::Respond;
        int delayMs = 0;
    };

    /** 接管监听器的新连接并安装请求解析与清理回调。 */
    explicit LoopbackHttpPeer(Behavior connectionBehavior = Behavior::Respond,
                              QObject *parent = nullptr)
        : QObject(parent), _connectionBehavior(connectionBehavior)
    {
        connect(&_server, &QTcpServer::newConnection, this, /** 接收全部待处理连接，按计划拒绝或安装读回调。 */ [this] {
            while (QTcpSocket *socket = _server.nextPendingConnection()) {
                _sockets.append(socket);
                if (_connectionBehavior == Behavior::ResetOnAccept) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                connect(socket, &QTcpSocket::readyRead, this, /** 累积完整请求头后按路径选择计划，每个连接只处理一次请求。 */ [this, socket] {
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
                    const auto respond = /** 若连接仍存活则写入完整 HTTP 响应并关闭发送连接。 */ [socket, body = plan.body] {
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
                connect(socket, &QObject::destroyed, this, /** 清除该连接的请求缓冲及跟踪引用。 */ [this, socket] {
                    _requests.remove(socket);
                    _sockets.removeAll(socket);
                });
            }
        });
    }

    /** 在随机回环端口监听并返回绑定结果。 */
    bool listen()
    {
        return _server.listen(QHostAddress::LocalHost, 0);
    }

    /** 组合当前监听端口及指定路径为 HTTP 测试地址。 */
    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(_server.serverPort())
                        .arg(path));
    }

    /** 为指定路径登记响应及故障计划。 */
    void setPlan(const QString &path, Plan plan)
    {
        _plans.insert(path, std::move(plan));
    }

    /** 统计尚未断开的测试连接。 */
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
    Behavior _connectionBehavior;
    QHash<QString, Plan> _plans;
    QHash<QTcpSocket *, QByteArray> _requests;
    QList<QPointer<QTcpSocket>> _sockets;
    // Destroy accepted sockets before the containers used by their destroyed callbacks.
    QTcpServer _server;
};

/** 构建带认证流程标识及有限期限的 Gate HTTP 请求。 */
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

/** 生成指定字节长度的有效 JSON 正文以覆盖大小边界。 */
QByteArray jsonBody(qsizetype size)
{
    const QByteArray prefix = "{\"value\":\"";
    const QByteArray suffix = "\"}";
    return prefix + QByteArray(size - prefix.size() - suffix.size(), 'x') + suffix;
}

} // namespace

/** 验证真实 HTTP 传输的身份、期限、大小、取消及资源释放合同。 */
class HttpTransportTests final : public QObject
{
    Q_OBJECT

private slots:
    /** 验证成功响应保持请求、模块及认证流程标识。 */
    void successPreservesRequestAndFlowIdentity();
    /** 验证连接被拒绝时有界结束且仅报告一次。 */
    void refusedConnectionHasOneBoundedOutcome();
    /** 验证无响应对端达到期限后被中止。 */
    void finiteDeadlineAbortsAnUnresponsivePeer();
    /** 验证非法 JSON 被拒绝且响应正文不泄露。 */
    void malformedJsonIsRejectedWithoutLeakingItsBody();
    /** 验证响应恰好达到上限时仍被接受。 */
    void maximumResponseIsAccepted();
    /** 验证响应超出上限一个字节即被拒绝。 */
    void oneByteOverMaximumIsRejected();
    /** 验证响应中途断连只产生一个网络错误。 */
    void peerClosingMidResponseHasOneNetworkOutcome();
    /** 验证显式取消仅产生一个终态。 */
    void explicitCancelHasExactlyOneTerminalOutcome();
    /** 验证旧流程的迟到响应无法完成新流程。 */
    void lateReplyFromAnOldFlowCannotCompleteTheNewFlow();
    /** 验证销毁传输器释放回复对象及回环套接字。 */
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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });
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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
    peer.setPlan(QStringLiteral("/maximum"), {jsonBody(GateHttpTransport::maxResponseBytes())});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

    transport.post(request(peer.url(QStringLiteral("/maximum")), 45));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QCOMPARE(results[0].terminal, GateHttpTerminal::Success);
    QCOMPARE(results[0].body.size(), GateHttpTransport::maxResponseBytes());
}

void HttpTransportTests::oneByteOverMaximumIsRejected()
{
    // Q04-HTTP-06
    LoopbackHttpPeer peer;
    QVERIFY(peer.listen());
    peer.setPlan(QStringLiteral("/oversized"), {jsonBody(GateHttpTransport::maxResponseBytes() + 1)});
    GateHttpTransport transport;
    QList<GateHttpResult> results;
    connect(&transport, &GateHttpTransport::finished, this,
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
            /** 收集 HTTP 终态结果，供身份、次数和错误断言。 */ [&results](const GateHttpResult &result) { results.append(result); });

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
