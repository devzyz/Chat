#include "tcpframedecoder.h"
#include "userstoragepaths.h"
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QScopeGuard>
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

/** 拥有一个独立客户端进程及本地控制管道，负责启动、命令和退出清理。 */
class OwnedClient
{
public:
    /** 终止尚未退出的所属进程并有限等待结束。 */
    ~OwnedClient()
    {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
    }
    /** 建立仅当前用户可访问的本地管道并启动客户端，等待就绪事件。 */
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
    /** 把控制命令编码为一行 JSON 并刷新管道。 */
    void send(const QJsonObject &object)
    {
        lastCommand = object.value("command").toString();
        lastCommandId = object.value("id").toInteger();
        pipe->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        pipe->flush();
    }
    /** 在四秒内读取一行控制响应，解析后返回对象。 */
    QJsonObject receive(int timeoutMs = 4000)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!buffer.contains('\n') && elapsed.elapsed() < timeoutMs) {
            buffer += pipe->readAll();
            if (buffer.contains('\n')) break;
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(pipe.get(), &QLocalSocket::readyRead, &loop, &QEventLoop::quit);
            QObject::connect(pipe.get(), &QLocalSocket::disconnected, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(static_cast<int>(timeoutMs - elapsed.elapsed()));
            loop.exec();
            buffer += pipe->readAll();
            if (pipe->state() == QLocalSocket::UnconnectedState && buffer.isEmpty()) break;
        }
        const auto end = buffer.indexOf('\n');
        if (end < 0) {
            // Do not dump command/reply bodies: login commands contain credentials.
            qWarning() << "control reply missing" << "command" << lastCommand << "id" << lastCommandId
                       << "elapsedMs" << elapsed.elapsed() << "processState" << process.state()
                       << "socketState" << pipe->state() << "bufferedBytes" << buffer.size();
            const auto diagnostics = process.readAllStandardError().split('\n');
            for (const auto &line : diagnostics) {
                const auto marker = line.indexOf("driver-storage-failure");
                if (marker >= 0) qWarning().noquote() << line.mid(marker).trimmed();
            }
            return {};
        }
        const auto object = QJsonDocument::fromJson(buffer.left(end)).object();
        buffer.remove(0, end + 1);
        return object;
    }
    /** 发送停止命令并核对响应身份、正常退出状态及退出码。 */
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
    QString lastCommand = "startup";
    qint64 lastCommandId = 0;
};

/** 提供真实回环 Gate 与 Chat 登录对端，记录账号请求和心跳。 */
class LoopbackLogin : public QObject
{
public:
    /** 为指定测试用户安装 HTTP 登录与聊天帧处理回调。 */
    explicit LoopbackLogin(int uid) : userId(uid)
    {
        QObject::connect(&gate, &QTcpServer::newConnection, this, /** 接收 Gate 连接并安装请求正文处理。 */ [this] {
            auto *peer = gate.nextPendingConnection();
            QObject::connect(peer, &QTcpSocket::readyRead, peer, /** 等待完整 HTTP JSON 后记录登录请求并返回测试用户选服结果。 */ [this, peer, buffer = QByteArray()]() mutable {
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
        QObject::connect(&chat, &QTcpServer::newConnection, this, /** 接收 Chat 连接并安装生产帧解码回调。 */ [this] {
            auto *peer = chat.nextPendingConnection();
            QObject::connect(peer, &QTcpSocket::readyRead, peer, /** 处理登录、心跳及增量同步请求，回送确定性的协议响应。 */ [this, peer, decoder = TcpFrameDecoder()]() mutable {
                for (const auto &frame : decoder.append(peer->readAll())) {
                    if (frame.messageId == 1020) { ++heartbeats; continue; }
                    if (frame.messageId == 1034 || frame.messageId == 1038) {
                        const auto request = QJsonDocument::fromJson(frame.body).object();
                        QJsonObject result{{"error", 0}, {"request_id", request["request_id"]},
                            {"chat_id", 71}, {"group_name", "storage lock group"}, {"owner_uid", userId},
                            {"group_revision", "1"}, {"membership_epoch", "1"}, {"joined_after_id", "0"},
                            {"group_state", "active"}, {"member_count", 2}};
                        const auto respond = /** 回送真实 TCP 群业务结果，SQLite 锁仅存在于客户端。 */ [peer, result, id = frame.messageId + 1] {
                            const auto payload = QJsonDocument(result).toJson(QJsonDocument::Compact);
                            QByteArray header(4, '\0');
                            qToBigEndian<quint16>(id, header.data());
                            qToBigEndian<quint16>(static_cast<quint16>(payload.size()), header.data() + 2);
                            peer->write(header + payload);
                        };
                        if (frame.messageId == 1038 && failGroupSync) QTimer::singleShot(300, peer, respond);
                        else respond();
                        continue;
                    }
                    if (frame.messageId == 1027) {
                        auto result = QJsonDocument::fromJson(frame.body).object();
                        const bool legacy = !result.contains("request_id");
                        result["error"] = result["chat_id"].toInt() == 71 && failGroupSync ? 87 : 0;
                        result["msgs"] = QJsonArray{};
                        result["next_cursor"] = result["after_id"];
                        result["load_more"] = false;
                        if (legacy) {
                            result["current_msg_id"] = 83;
                            result["msgs"] = QJsonArray{QJsonObject{{"message_id", 83}, {"send_id", 42},
                                {"recv_id", userId}, {"content", "legacy history"}, {"created_at", 1700000000},
                                {"msg_uuid", "00000000-0000-4000-8000-000000000083"}, {"status", 0}}};
                            // 同一响应 ID 的后台同步失败不能结算显式历史命令。
                            const auto sync = QJsonDocument(QJsonObject{{"request_id", "unrelated-sync"},
                                {"chat_id", result["chat_id"]}, {"error", 87}}).toJson(QJsonDocument::Compact);
                            QByteArray syncHeader(4, '\0');
                            qToBigEndian<quint16>(1028, syncHeader.data());
                            qToBigEndian<quint16>(static_cast<quint16>(sync.size()), syncHeader.data() + 2);
                            peer->write(syncHeader + sync);
                        }
                        const auto payload = QJsonDocument(result).toJson(QJsonDocument::Compact);
                        QByteArray header(4, '\0');
                        qToBigEndian<quint16>(1028, header.data());
                        qToBigEndian<quint16>(static_cast<quint16>(payload.size()), header.data() + 2);
                        peer->write(header + payload);
                        if (legacy) {
                            // 重放且改变游标的未请求旧响应不能再改变模型。
                            result["current_msg_id"] = 999;
                            result["msgs"] = QJsonArray{};
                            const auto unsolicited = QJsonDocument(result).toJson(QJsonDocument::Compact);
                            qToBigEndian<quint16>(static_cast<quint16>(unsolicited.size()), header.data() + 2);
                            peer->write(header + unsolicited);
                        }
                        continue;
                    }
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
                        const auto sendFrame = /** 将响应 JSON 编码为大端聊天帧发送给该连接。 */ [peer](int id, const QJsonObject &object) {
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
                        if (frame.messageId == 1023) ++createFrames;
                        if (frame.messageId == 1016) {
                            ++sentFrames;
                            if (dropNextText) { dropNextText = false; peer->abort(); continue; }
                            sentBody = request;
                            result["from_uid"] = userId;
                            result["to_uid"] = request["to_uid"];
                            result["attempt_id"] = request["attempt_id"];
                            const auto uuid = request["text_array"].toArray().first().toObject()["msg_uuid"].toString();
                            if (!messageIds.contains(uuid)) messageIds.insert(uuid, 81 + messageIds.size());
                            result["uuid_msgId"] = QJsonArray{QJsonObject{
                                {"msg_uuid", uuid}, {"message_id", messageIds.value(uuid)}}};
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
                        {"name", "synthetic"}, {"token", "synthetic-login-token"},
                        {"chat_list", QJsonArray{QJsonObject{{"chat_id", 9}, {"type", "private"},
                            {"user1_id", userId}, {"user2_id", 99}}}}}).toJson(QJsonDocument::Compact);
                    QByteArray response(4, '\0');
                    qToBigEndian<quint16>(1006, response.data());
                    qToBigEndian<quint16>(static_cast<quint16>(body.size()), response.data() + 2);
                    peer->write(response + body);
                }
            });
        });
    }
    /** 同时在随机回环端口监听 Gate 和 Chat 服务。 */
    bool listen() { return gate.listen(QHostAddress::LocalHost, 0) && chat.listen(QHostAddress::LocalHost, 0); }
    /** 返回回环 Gate 的 HTTP 入口。 */
    QString url() const { return QString("http://127.0.0.1:%1").arg(gate.serverPort()); }
    int userId;
    QTcpServer gate, chat;
    QJsonObject gateBody, chatBody, sentBody;
    QJsonObject applicationBody, acceptanceBody;
    QHash<QString, int> messageIds;
    int heartbeats = 0;
    int sentFrames = 0;
    int createFrames = 0;
    bool dropNextText = false;
    bool failGroupSync = false;
};

/** 验证独立客户端进程的登录隔离、控制协议及生命周期。 */
class SessionDriverTests : public QObject
{
    Q_OBJECT
private slots:
    /** 验证两个真实客户端分别保持所属账号与端点。 */
    void productionLoginKeepsAccountsAndEndpointsSeparate()
    {
        LoopbackLogin unavailable(QRandomGenerator::global()->bounded(100000, 2000000000));
        QVERIFY(unavailable.listen());
        const auto blockedRoot = UserStoragePaths::accountRoot(UserStoragePaths::dataRoot(),
            unavailable.url(), unavailable.userId);
        QVERIFY(QDir().mkpath(QFileInfo(blockedRoot).absolutePath()));
        QFile obstruction(blockedRoot);
        QVERIFY(obstruction.open(QIODevice::WriteOnly));
        obstruction.close();
        const auto removeObstruction = qScopeGuard(/** 清理仅本次创建的阻塞文件，不删除账号目录。 */ [&] {
            QFile::remove(blockedRoot);
        });
        OwnedClient recovering;
        QVERIFY(recovering.start());
        const QJsonObject login{{"id", 1}, {"command", "login"}, {"gate", unavailable.url()},
            {"email", "storage@example.invalid"}, {"password", "fixture-only"}};
        recovering.send(login);
        QCOMPARE(recovering.receive(10000).value("status").toString(), QString("login-failed"));
        recovering.send({{"id", 2}, {"command", "snapshot"}});
        QVERIFY(!recovering.receive().value("active").toBool());
        QVERIFY(QFile::remove(blockedRoot));
        auto retryLogin = login; retryLogin["id"] = 3;
        recovering.send(retryLogin);
        QCOMPARE(recovering.receive(10000).value("status").toString(), QString("authenticated"));
        recovering.send({{"id", 4}, {"command", "create"}, {"toUid", 42}});
        QCOMPARE(recovering.receive().value("chatId").toInt(), 7);
        QVERIFY(recovering.stop(5));

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
        const auto aliceReply = alice.receive(10000);
        const auto bobReply = bob.receive(10000);
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
        const auto created = alice.receive();
        QCOMPARE(first.createFrames, 1);
        QCOMPARE(created.value("chatId").toInt(), 7);
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        alice.send({{"id", 3}, {"command", "send"}, {"chatId", 7}, {"toUid", 42},
                    {"uuid", uuid}, {"text", QString::fromUtf8("跨实例 🙂\nsecond line")}, {"copies", 2}});
        const auto sendResult = alice.receive();
        QCOMPARE(sendResult.value("error").toInt(-1), 0);
        QCOMPARE(first.sentBody.value("from_uid").toInt(), 41);
        QCOMPARE(first.sentFrames, 2);
        alice.send({{"id", 4}, {"command", "send"}, {"chatId", 7}, {"toUid", 42},
                    {"uuid", uuid}, {"text", QString::fromUtf8("跨实例 🙂\nsecond line")}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(first.sentFrames, 3); // Explicit committed replay remains a server idempotency probe.
        alice.send({{"id", 5}, {"command", "snapshot"}, {"chatId", 7}});
        const auto rows = alice.receive().value("messages").toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject()["messageId"].toString(), QString("81"));
        QCOMPARE(rows.first().toObject()["uuid"].toString(), uuid);
        QVERIFY(!rows.first().toObject().contains("text"));
        alice.send({{"id", 6}, {"command", "apply"}, {"toUid", 42},
                    {"description", "hello peer"}, {"backname", "peer alias"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(first.applicationBody.value("fromuid").toInt(), 41);
        QCOMPARE(first.applicationBody.value("touid").toInt(), 42);
        QCOMPARE(first.applicationBody.value("description").toString(), QString("hello peer"));
        int nextCommand = 7;
        QJsonObject application;
        const auto applicationStored = /** 等待异步目录事务可从用户状态观察到，不假定通知与请求 ACK 同步完成。 */ [&] {
            alice.send({{"id", nextCommand++}, {"command", "snapshot"}, {"otherUid", 42}});
            application = alice.receive();
            return application.value("applied").toBool();
        };
        QTRY_VERIFY_WITH_TIMEOUT(applicationStored(), 3000);
        QVERIFY(!application.value("friend").toBool());
        alice.send({{"id", nextCommand++}, {"command", "accept"}, {"toUid", 42},
                    {"description", "accepted"}, {"backname", "accepted alias"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        QCOMPARE(first.acceptanceBody.value("authuid").toInt(), 41);
        QCOMPARE(first.acceptanceBody.value("applyuid").toInt(), 42);
        QCOMPARE(first.acceptanceBody.value("applyinfo").toObject().value("backname").toString(),
                 QString("original alias"));
        QCOMPARE(first.acceptanceBody.value("authinfo").toObject().value("backname").toString(),
                 QString("accepted alias"));
        alice.send({{"id", nextCommand++}, {"command", "snapshot"}, {"otherUid", 42}});
        const auto accepted = alice.receive();
        QVERIFY(accepted.value("friend").toBool());
        QCOMPARE(accepted.value("chatId").toInt(), 7);
        QVERIFY(!accepted.contains("token"));
        alice.send({{"id", nextCommand++}, {"command", "accept"}, {"toUid", 999},
                    {"description", "unknown"}, {"backname", "unknown"}});
        QCOMPARE(alice.receive().value("status").toString(), QString("no-application"));
        QTRY_VERIFY_WITH_TIMEOUT(first.heartbeats > 0 && second.heartbeats > 0, 12000);
        first.dropNextText = true;
        const QString uncertainUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        alice.send({{"id", nextCommand++}, {"command", "send"}, {"chatId", 7}, {"toUid", 42},
                    {"uuid", uncertainUuid}, {"text", QString::fromUtf8("\u8de8\u5b9e\u4f8b \U0001f642\nsecond line")}});
        QCOMPARE(alice.receive().value("status").toString(), QString("disconnected"));
        alice.send({{"id", nextCommand++}, {"command", "login"}, {"gate", first.url()},
                    {"email", "alice@example.invalid"}, {"password", "fixture-only"}});
        QCOMPARE(alice.receive(10000).value("status").toString(), QString("authenticated"));
        QTRY_COMPARE_WITH_TIMEOUT(first.sentFrames, 5, 3000);
        alice.send({{"id", nextCommand++}, {"command", "snapshot"}, {"chatId", 7}});
        const auto recovered = alice.receive().value("messages").toArray();
        QCOMPARE(recovered.size(), 2);
        QCOMPARE(recovered.last().toObject()["uuid"].toString(), uncertainUuid);
        QCOMPARE(first.sentBody["attempt_id"].toString(), QString("2"));
        alice.send({{"id", nextCommand++}, {"command", "history"}, {"chatId", 8}, {"cursor", "0"}});
        QCOMPARE(alice.receive().value("error").toInt(-1), 0);
        alice.send({{"id", nextCommand++}, {"command", "snapshot"}, {"chatId", 8}});
        const auto history = alice.receive();
        QCOMPARE(history.value("cursor").toString(), QString("83"));
        QCOMPARE(history.value("messages").toArray().size(), 1);
        QCOMPARE(history.value("messages").toArray().first().toObject()["uuid"].toString(),
                 QString("00000000-0000-4000-8000-000000000083"));
        alice.send({{"id", nextCommand++}, {"command", "local-history"}, {"chatId", 7}, {"before", "0"}});
        const auto stored = alice.receive();
        QCOMPARE(stored["error"].toInt(-1), 0);
        QVERIFY(!stored["result"].toObject()["messages"].toArray().isEmpty());
        alice.send({{"id", nextCommand++}, {"command", "search"}, {"chatId", 7}, {"text", "second line"}, {"before", "0"}});
        const auto search = alice.receive();
        QCOMPARE(search["error"].toInt(-1), 0);
        const auto found = search["result"].toObject()["messages"].toArray();
        QVERIFY(!found.isEmpty());
        bool hasRecovered = false;
        for (const auto &row : found) hasRecovered |= row.toObject()["uuid"].toString() == uncertainUuid;
        QVERIFY(hasRecovered);
        alice.send({{"id", nextCommand++}, {"command", "directory-find"}, {"kind", "contacts"}, {"text", "accepted alias"}, {"after", 0}});
        const auto directory = alice.receive();
        QCOMPARE(directory["error"].toInt(-1), 0);
        QCOMPARE(directory["result"].toObject()["rows"].toArray().size(), 1);
        const auto groupUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        alice.send({{"id", nextCommand++}, {"command", "group-create"}, {"uuid", groupUuid},
            {"name", "storage lock group"}, {"members", QJsonArray{42}}});
        QCOMPARE(alice.receive()["error"].toInt(-1), 0);
        first.failGroupSync = true;
        const auto manageUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject manage{{"command", "group-manage"}, {"chatId", 71}, {"uuid", manageUuid},
            {"version", "1"}, {"operation", "rename"}, {"params", QJsonObject{{"name", "renamed"}}}};
        auto locked = manage; locked["id"] = nextCommand++; locked["localSaveFailure"] = true;
        alice.send(locked);
        const auto failedSave = alice.receive(15000);
        QCOMPARE(failedSave["error"].toInt(-1), 0);
        QVERIFY(failedSave["result"].toObject()["local_save_failed"].toBool());
        alice.send({{"id", nextCommand++}, {"command", "storage-unlock"}});
        QCOMPARE(alice.receive()["error"].toInt(-1), 0);
        first.failGroupSync = false;
        auto retried = manage; retried["id"] = nextCommand++;
        alice.send(retried);
        const auto saved = alice.receive(15000);
        QCOMPARE(saved["error"].toInt(-1), 0);
        QVERIFY(!saved["result"].toObject()["local_save_failed"].toBool());
        alice.send({{"id", nextCommand++}, {"command", "logout"}});
        QCOMPARE(alice.receive()["status"].toString(), QString("logged-out"));
        QVERIFY(alice.stop(nextCommand++));
        bob.send({{"id", 2}, {"command", "snapshot"}});
        QCOMPARE(bob.receive().value("uid").toInt(), 42);
        QVERIFY(bob.stop(3));
    }
    /** 验证两个客户端进程的生命周期相互独立。 */
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
    /** 验证非法、超长及重放控制命令使客户端失败关闭。 */
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
    /** 验证控制器断开后客户端在期限内退出。 */
    void lostControllerTerminatesClient()
    {
        OwnedClient client;
        QVERIFY(client.start());
        client.pipe->abort();
        QVERIFY(client.process.waitForFinished(3000));
        QCOMPARE(client.process.exitCode(), 2);
    }
    /** 验证停止命令取消尚未完成的生产 HTTP 请求并关闭连接。 */
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
