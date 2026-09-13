#include "tcpframedecoder.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <memory>

class OwnedClient
{
public:
    ~OwnedClient()
    {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
    }
    bool start()
    {
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen("chat-driver-" + QUuid::createUuid().toString(QUuid::WithoutBraces))) return false;
        QString executable = QCoreApplication::applicationDirPath() + "/chat_e2e_client";
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        process.start(executable, {"--control", server.fullServerName()});
        if (!process.waitForStarted(3000) || !server.waitForNewConnection(3000)) return false;
        pipe.reset(server.nextPendingConnection());
        return receive().value("event") == "ready";
    }
    void send(const QJsonObject &object)
    {
        pipe->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        pipe->flush();
    }
    QJsonObject receive()
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!buffer.contains('\n') && elapsed.elapsed() < 4000) {
            buffer += pipe->readAll();
            if (buffer.contains('\n')) break;
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(pipe.get(), &QLocalSocket::readyRead, &loop, &QEventLoop::quit);
            QObject::connect(pipe.get(), &QLocalSocket::disconnected, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(static_cast<int>(4000 - elapsed.elapsed()));
            loop.exec();
            buffer += pipe->readAll();
            if (pipe->state() == QLocalSocket::UnconnectedState && buffer.isEmpty()) break;
        }
        const auto end = buffer.indexOf('\n');
        if (end < 0) return {};
        const auto object = QJsonDocument::fromJson(buffer.left(end)).object();
        buffer.remove(0, end + 1);
        return object;
    }
    bool stop(qint64 id)
    {
        send(QJsonObject{{"id", id}, {"command", "stop"}});
        const auto response = receive();
        const bool ended = process.state() == QProcess::NotRunning || process.waitForFinished(3000);
        if (!ended || process.exitCode() != 0 || response.value("status") != "stopped") {
            qWarning() << "stop result" << ended << process.exitCode() << response;
        }
        return response.value("id").toInteger() == id && response.value("status") == "stopped" &&
            ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    QLocalServer server;
    QProcess process;
    std::unique_ptr<QLocalSocket> pipe;
    QByteArray buffer;
};

class LoopbackLogin : public QObject
{
public:
    explicit LoopbackLogin(int uid) : userId(uid)
    {
        QObject::connect(&gate, &QTcpServer::newConnection, this, [this] {
            auto *peer = gate.nextPendingConnection();
            QObject::connect(peer, &QTcpSocket::readyRead, peer, [this, peer, buffer = QByteArray()]() mutable {
                buffer += peer->readAll();
                const auto end = buffer.indexOf("\r\n\r\n");
                if (end < 0) return;
                const auto body = QJsonDocument::fromJson(buffer.mid(end + 4));
                if (!body.isObject()) return;
                gateBody = body.object();
                const auto response = QJsonDocument(QJsonObject{{"error", 0}, {"uid", userId},
                    {"host", "127.0.0.1"}, {"port", QString::number(chat.serverPort())},
                    {"token", "synthetic-login-token"}}).toJson(QJsonDocument::Compact);
                peer->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                            QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
                peer->disconnectFromHost();
            });
        });
        QObject::connect(&chat, &QTcpServer::newConnection, this, [this] {
            auto *peer = chat.nextPendingConnection();
            QObject::connect(peer, &QTcpSocket::readyRead, peer, [this, peer, decoder = TcpFrameDecoder()]() mutable {
                for (const auto &frame : decoder.append(peer->readAll())) {
                    if (frame.messageId == 1020) { ++heartbeats; continue; }
                    if (frame.messageId == 1009 || frame.messageId == 1013) {
                        const auto request = QJsonDocument::fromJson(frame.body).object();
                        QJsonObject result{{"error", 0}};
                        if (frame.messageId == 1009) {
                            applicationBody = request;
                        } else {
                            acceptanceBody = request;
                            result = request;
                            result["error"] = 0;
                            result["chatid"] = 7;
                        }
                        const auto sendFrame = [peer](int id, const QJsonObject &object) {
                            const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
                            QByteArray header(4, '\0');
                            qToBigEndian<quint16>(id, header.data());
                            qToBigEndian<quint16>(static_cast<quint16>(payload.size()), header.data() + 2);
                            peer->write(header + payload);
                        };
                        sendFrame(frame.messageId + 1, result);
                        if (frame.messageId == 1009) sendFrame(1011, QJsonObject{{"error", 0},
                            {"fromuid", 42}, {"touid", userId}, {"applyname", "peer"},
                            {"description", "synthetic application"}, {"backname", "original alias"}});
                        continue;
                    }
                    if (frame.messageId == 1023 || frame.messageId == 1016) {
                        const auto request = QJsonDocument::fromJson(frame.body).object();
                        QJsonObject result{{"error", 0}, {"chat_id", 7}, {"self_id", userId}, {"other_id", 42}};
                        if (frame.messageId == 1016) {
                            sentBody = request;
                            result["uuid_msgId"] = QJsonArray{QJsonObject{
                                {"msg_uuid", request["text_array"].toArray().first().toObject()["msg_uuid"]},
                                {"message_id", 81}}};
                        }
                        const auto payload = QJsonDocument(result).toJson(QJsonDocument::Compact);
                        QByteArray header(4, '\0');
                        qToBigEndian<quint16>(frame.messageId + 1, header.data());
                        qToBigEndian<quint16>(static_cast<quint16>(payload.size()), header.data() + 2);
                        peer->write(header + payload);
                        continue;
                    }
                    if (frame.messageId != 1005) continue;
                    chatBody = QJsonDocument::fromJson(frame.body).object();
                    const auto body = QJsonDocument(QJsonObject{{"error", 0}, {"uid", userId},
                        {"name", "synthetic"}, {"token", "synthetic-login-token"}}).toJson(QJsonDocument::Compact);
                    QByteArray response(4, '\0');
                    qToBigEndian<quint16>(1006, response.data());
                    qToBigEndian<quint16>(static_cast<quint16>(body.size()), response.data() + 2);
                    peer->write(response + body);
                }
            });
        });
    }
    bool listen() { return gate.listen(QHostAddress::LocalHost, 0) && chat.listen(QHostAddress::LocalHost, 0); }
    QString url() const { return QString("http://127.0.0.1:%1").arg(gate.serverPort()); }
    int userId;
    QTcpServer gate, chat;
    QJsonObject gateBody, chatBody, sentBody;
    QJsonObject applicationBody, acceptanceBody;
    int heartbeats = 0;
};

class SessionDriverTests : public QObject
{
    Q_OBJECT
private slots:
    void productionLoginKeepsAccountsAndEndpointsSeparate()
    {
        LoopbackLogin first(41), second(42);
        QVERIFY(first.listen());
        QVERIFY(second.listen());
        OwnedClient alice, bob;
        QVERIFY(alice.start());
        QVERIFY(bob.start());
        alice.send({{"id", 1}, {"command", "login"}, {"gate", first.url()},
                    {"email", "alice@example.invalid"}, {"password", "fixture-only"}});
        bob.send({{"id", 1}, {"command", "login"}, {"gate", second.url()},
                  {"email", "bob@example.invalid"}, {"password", "fixture-only"}});
        const auto aliceReply = alice.receive();
        const auto bobReply = bob.receive();
        QCOMPARE(aliceReply.value("status").toString(), QString("authenticated"));
        QCOMPARE(bobReply.value("status").toString(), QString("authenticated"));
        QCOMPARE(aliceReply.value("uid").toInt(), 41);
        QCOMPARE(bobReply.value("uid").toInt(), 42);
        QCOMPARE(aliceReply.value("port").toInt(), first.chat.serverPort());
        QCOMPARE(bobReply.value("port").toInt(), second.chat.serverPort());
        QCOMPARE(first.chatBody.value("uid").toInt(), 41);
        QCOMPARE(second.chatBody.value("uid").toInt(), 42);
        QCOMPARE(first.chatBody.value("token").toString(), QString("synthetic-login-token"));
        QVERIFY(!aliceReply.contains("token"));
        QVERIFY(!bobReply.contains("password"));
        QVERIFY(first.gateBody.value("password").toString() != "fixture-only");
        alice.send({{"id", 2}, {"command", "create"}, {"toUid", 42}});
        QCOMPARE(alice.receive().value("chatId").toInt(), 7);
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        alice.send({{"id", 3}, {"command", "send"}, {"chatId", 7}, {"toUid", 42},
                    {"uuid", uuid}, {"text", QString::fromUtf8("跨实例 🙂\nsecond line")}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(first.sentBody.value("from_uid").toInt(), 41);
        alice.send({{"id", 4}, {"command", "snapshot"}, {"chatId", 7}});
        const auto rows = alice.receive().value("messages").toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject()["messageId"].toString(), QString("81"));
        QCOMPARE(rows.first().toObject()["uuid"].toString(), uuid);
        QVERIFY(!rows.first().toObject().contains("text"));
        alice.send({{"id", 5}, {"command", "apply"}, {"toUid", 42},
                    {"description", "hello peer"}, {"backname", "peer alias"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(first.applicationBody.value("fromuid").toInt(), 41);
        QCOMPARE(first.applicationBody.value("touid").toInt(), 42);
        QCOMPARE(first.applicationBody.value("description").toString(), QString("hello peer"));
        alice.send({{"id", 6}, {"command", "snapshot"}, {"otherUid", 42}});
        const auto application = alice.receive();
        QVERIFY(application.value("applied").toBool());
        QVERIFY(!application.value("friend").toBool());
        alice.send({{"id", 7}, {"command", "accept"}, {"toUid", 42},
                    {"description", "accepted"}, {"backname", "accepted alias"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(first.acceptanceBody.value("authuid").toInt(), 41);
        QCOMPARE(first.acceptanceBody.value("applyuid").toInt(), 42);
        QCOMPARE(first.acceptanceBody.value("applyinfo").toObject().value("backname").toString(),
                 QString("original alias"));
        QCOMPARE(first.acceptanceBody.value("authinfo").toObject().value("backname").toString(),
                 QString("accepted alias"));
        alice.send({{"id", 8}, {"command", "snapshot"}, {"otherUid", 42}});
        const auto accepted = alice.receive();
        QVERIFY(accepted.value("friend").toBool());
        QCOMPARE(accepted.value("chatId").toInt(), 7);
        QVERIFY(!accepted.contains("token"));
        alice.send({{"id", 9}, {"command", "accept"}, {"toUid", 999},
                    {"description", "unknown"}, {"backname", "unknown"}});
        QCOMPARE(alice.receive().value("status").toString(), QString("no-application"));
        QTRY_VERIFY_WITH_TIMEOUT(first.heartbeats > 0 && second.heartbeats > 0, 12000);
        QVERIFY(alice.stop(10));
        bob.send({{"id", 2}, {"command", "snapshot"}});
        QCOMPARE(bob.receive().value("uid").toInt(), 42);
        QVERIFY(bob.stop(3));
    }
    void twoProcessesHaveIndependentLifetimes()
    {
        LoopbackLogin fixture(41);
        QVERIFY(fixture.listen());
        OwnedClient alice, bob;
        QVERIFY(alice.start());
        QVERIFY(bob.start());
        QVERIFY(alice.process.processId() != bob.process.processId());
        alice.send({{"id", 1}, {"command", "snapshot"}});
        const auto first = alice.receive();
        QCOMPARE(first.value("id").toInt(), 1);
        QCOMPARE(first.value("uid").toInt(), 0);
        QCOMPARE(first.value("active").toBool(), false);
        alice.send({{"id", 2}, {"command", "verify"}, {"gate", fixture.url()}, {"email", "alice@example.invalid"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(fixture.gateBody.value("email").toString(), QString("alice@example.invalid"));
        alice.send({{"id", 3}, {"command", "register"}, {"gate", fixture.url()},
                    {"email", "alice@example.invalid"}, {"name", "alice"},
                    {"password", "fixture-only"}, {"code", "synthetic-code"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(fixture.gateBody.value("user").toString(), QString("alice"));
        QCOMPARE(fixture.gateBody.value("passwd"), fixture.gateBody.value("confirm"));
        QVERIFY(fixture.gateBody.value("passwd").toString() != "fixture-only");
        QVERIFY(alice.stop(4));
        bob.send({{"id", 1}, {"command", "snapshot"}});
        QCOMPARE(bob.receive().value("status").toString(), QString("snapshot"));
        QVERIFY(bob.stop(2));
    }
    void malformedAndReplayedCommandsFailClosed()
    {
        for (const auto &payload : {QByteArray("not-json\n"), QByteArray(8193, 'x'),
                                   QByteArray("{\"id\":1,\"command\":\"snapshot\"}\n")}) {
            OwnedClient client;
            QVERIFY(client.start());
            client.send({{"id", 1}, {"command", "snapshot"}});
            QCOMPARE(client.receive().value("id").toInt(), 1);
            client.pipe->write(payload);
            client.pipe->flush();
            QVERIFY(client.process.waitForFinished(3000));
            QCOMPARE(client.process.exitCode(), 2);
        }
    }
    void lostControllerTerminatesClient()
    {
        OwnedClient client;
        QVERIFY(client.start());
        client.pipe->abort();
        QVERIFY(client.process.waitForFinished(3000));
        QCOMPARE(client.process.exitCode(), 2);
    }
    void stopCancelsPendingProductionHttp()
    {
        QTcpServer gate;
        QVERIFY(gate.listen(QHostAddress::LocalHost, 0));
        OwnedClient client;
        QVERIFY(client.start());
        client.send({{"id", 1}, {"command", "login"},
                     {"gate", QString("http://127.0.0.1:%1").arg(gate.serverPort())},
                     {"email", "alice@example.invalid"}, {"password", "fixture-only"}});
        QVERIFY(gate.waitForNewConnection(3000));
        std::unique_ptr<QTcpSocket> peer(gate.nextPendingConnection());
        QVERIFY(client.stop(2));
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QAbstractSocket::UnconnectedState, 3000);
        QVERIFY(!client.process.readAllStandardOutput().contains("fixture-only"));
        QVERIFY(!client.process.readAllStandardError().contains("fixture-only"));
    }
};

QTEST_GUILESS_MAIN(SessionDriverTests)
#include "session_driver_tests.moc"
