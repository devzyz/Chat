#include "clientloginflow.h"
#include "clientsession.h"
#include "clientmessage.h"
#include "clientrequests.h"
#include "messagemodelstore.h"
#include "messageservice.h"
#include "tcpmgr.h"
#include "usermgr.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <QLocalSocket>
#include <QTimer>
#include <QTemporaryDir>
#include "localmessagestore.h"
#include <cmath>

// The control channel carries commands, never a substitute Chat wire protocol.
/** @brief 通过本地控制通道驱动真实客户端登录、好友与消息流程。 */
class ClientDriver final : public QObject
{
public:
    /** @brief 连接控制通道，并将生产客户端事件转换为测试响应。 */
    explicit ClientDriver(const QString &endpoint)
        : _login(_auth), _session(this)
    {
        _socket.setReadBufferSize(8193);
        _watchdog.setSingleShot(true);
        _commandDeadline.setSingleShot(true);
        connect(&_commandDeadline, &QTimer::timeout, this, /** 看门狗到期后终止失去进展的驱动进程。 */ [this] { finish(2); });
        connect(&_accounts, &GateHttpTransport::finished, this, /** 只结算当前控制命令对应的 HTTP 流程，并返回安全业务状态。 */ [this](const GateHttpResult &result) {
            if (!_pendingId || static_cast<quint64>(_pendingId) != result.flowId) return;
            const auto object = QJsonDocument::fromJson(result.body).object();
            send(QJsonObject{{"id", _pendingId}, {"status", "completed"},
                {"error", result.terminal == GateHttpTerminal::Success ? object.value("error").toInt(-1) : -1}});
            _pendingId = 0;
        });
        connect(&_watchdog, &QTimer::timeout, this, /** 控制连接或命令发生不可恢复错误时退出。 */ [this] { finish(2); });
        connect(&_socket, &QLocalSocket::errorOccurred, this, /** 控制连接或命令发生不可恢复错误时退出。 */ [this] { finish(2); });
        connect(&_socket, &QLocalSocket::disconnected, this, /** 控制通道断开时按是否正常停机选择退出码。 */ [this] { finish(_stopping ? 0 : 2); });
        connect(&_socket, &QLocalSocket::connected, this, /** 控制连接建立后启动看门狗并发布就绪事件。 */ [this] {
            _watchdog.start(60000);
            send(QJsonObject{{"event", "ready"}, {"pid", QCoreApplication::applicationPid()}, {"format", 1}});
        });
        connect(&_socket, &QLocalSocket::readyRead, this, /** 控制管道有数据时读取并处理命令。 */ [this] { read(); });
        connect(&_login, &ClientLoginFlow::authenticated, this, /** 登录成功后隔离账号模型，开始会话并回复已认证状态。 */ [this](AuthFlowId) {
            const int uid = UserMgr::instance()->uid();
            if (_lastUid && _lastUid != uid) {
                _messages = MessageModelStore{};
                _committedUuids.clear();
            }
            _lastUid = uid;
            _session.beginSession();
            reply(_pendingId, "authenticated");
            _pendingId = 0;
        });
        connect(&_login, &ClientLoginFlow::failed, this, /** 登录失败后返回结构化错误并清除待处理命令。 */ [this](AuthFlowId, AuthError error) {
            send(QJsonObject{{"id", _pendingId}, {"status", "login-failed"}, {"error", static_cast<int>(error)}});
            _pendingId = 0;
        });
        connect(TcpMgr::instance().get(), &TcpMgr::connectionClosed, this, /** 非预期断连时重置会话并终止正在等待响应的控制命令。 */ [this](bool expected) {
            if (!expected && _session.isActive()) {
                _session.resetSession(SessionResetReason::UnexpectedDisconnect);
                if (_pendingId && _expectedResponse >= 0) {
                    _commandDeadline.stop();
                    send(QJsonObject{{"id", _pendingId}, {"status", "disconnected"}, {"error", -1}});
                    _pendingId = 0;
                    _expectedResponse = -1;
                }
            }
        });
        connect(TcpMgr::instance().get(), &TcpMgr::forcedOffline, this,
                /** 被踢出登录时以对应原因重置会话。 */ [this] { _session.resetSession(SessionResetReason::Kicked); });
        const auto tcp = TcpMgr::instance();
        auto *service = UserMgr::instance()->messages();
        connect(service, &MessageService::messagesChanged, this,
            /** 回执变化后从持久存储刷新所属会话历史。 */ [service](int chatId) { service->loadHistory(chatId); });
        connect(service, &MessageService::historyLoaded, this,
            /** 只用本账号已确认历史对已有本地发送进行确认，保留驱动的显式分页流程。 */ [this](int chatId, qint64, const QVector<StoredMessage> &rows, bool) {
                // Preserve the harness's explicit legacy-history traversal; reconcile existing sends only.
                auto *model = _messages.find(chatId);
                if (!model) return;
                for (const auto &row : rows) {
                    if (row.senderId != _lastUid || row.messageId <= 0
                        || model->rowForClientMessageId(row.clientMessageId, row.senderId) < 0) continue;
                    model->acknowledgeMessage(row.clientMessageId, row.messageId, DeliveryStatus::Sent, row.senderId);
                    _committedUuids.insert(row.clientMessageId);
                }
            });
        connect(service, &MessageService::sendFailed, this, /** 文本发送失败时终止对应控制命令并返回业务失败。 */ [this](int, const QVector<QString> &) {
            if (!_pendingId || _expectedResponse != ID_TEXT_CHAT_MSG_RSP) return;
            _commandDeadline.stop();
            send(QJsonObject{{"id", _pendingId}, {"status", "completed"}, {"error", 1}});
            _pendingId = 0;
            _expectedResponse = -1;
        });
        connect(UserMgr::instance()->messages(), &MessageService::sendRequested, this,
            /** 在重复提交场景重新发送相同生产文本请求。 */ [this, tcp](const QJsonObject &request) {
                if (_pendingId && _expectedResponse == ID_TEXT_CHAT_MSG_RSP && _responsesRemaining == 2)
                    emit tcp->sendRequested(ID_TEXT_CHAT_MSG_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
            });
        connect(tcp.get(), &TcpMgr::friendApplicationReceived, this,
                /** 将收到的好友申请登记到用户状态。 */ [](const std::shared_ptr<ApplyInfo> &apply) {
            if (apply) UserMgr::instance()->addFriendApplication(apply->_apply_uid, apply);
        });
        connect(tcp.get(), &TcpMgr::chatMessagesReceived, this,
                /** 把实时消息转换并追加到所属会话模型。 */ [this](int, int, int, std::vector<std::shared_ptr<ChatDataBase>> &messages) {
            for (const auto &message : messages) {
                const auto record = clientMessageRecord(message);
                _messages.getOrCreate(record.chatId)->appendMessage(record);
            }
        });
        connect(tcp.get(), &TcpMgr::chatHistoryLoaded, this,
                /** 将历史页转换后应用模型，记录拒绝结果供控制响应。 */ [this](int chatId, std::vector<std::shared_ptr<ChatDataBase>> messages, bool more, qint64 cursor) {
            QVector<MessageRecord> records;
            for (const auto &message : messages) records.push_back(clientMessageRecord(message));
            if (!_messages.applyHistory(chatId, records, more, cursor)) _historyRejected = true;
        });
        connect(tcp.get(), &TcpMgr::messagesAcknowledged, this,
                /** 按当前发送者身份应用 ACK 并记录已提交 UUID。 */ [this](int chatId, QVector<MessageAcknowledgement> acks) {
            _messages.acknowledge(chatId, acks, UserMgr::instance()->uid());
            for (const auto &ack : acks) _committedUuids.insert(ack.clientMessageId);
        });
        connect(tcp.get(), &TcpMgr::messagesFailed, this,
                /** 按当前发送者标记指定批次消息失败。 */ [this](int chatId, const QVector<QString> &ids) { _messages.markFailed(chatId, ids, UserMgr::instance()->uid()); });
        connect(tcp.get(), &TcpMgr::privateChatCreated, this,
                /** 保存最近创建或获取的聊天编号。 */ [this](const std::shared_ptr<ChatInfo> &chat) { if (chat) _lastChatId = chat->getChatId(); });
        connect(tcp.get(), &TcpMgr::requestCompleted, this, /** 累计预期业务响应，全部到达后结算控制命令并清除期限。 */ [this](ReqId id, int error) {
            if (!_pendingId || id != _expectedResponse) return;
            if (error != 0) _commandError = error;
            if (--_responsesRemaining > 0) return;
            _commandDeadline.stop();
            send(QJsonObject{{"id", _pendingId}, {"status", "completed"},
                {"error", _historyRejected ? -1 : _commandError}, {"chatId", _lastChatId}});
            _pendingId = 0;
            _expectedResponse = -1;
        });
        _watchdog.start(5000);
        _socket.connectToServer(endpoint);
    }

private:
    /** 幂等取消认证、重置账号和会话、关闭控制管道并退出事件循环。 */
    void finish(int code)
    {
        if (_finished) return;
        _finished = true;
        _pendingId = 0;
        _login.cancel();
        _accounts.reset();
        _session.resetSession(SessionResetReason::Logout);
        TcpMgr::instance()->resetConnection(true);
        UserMgr::instance()->resetSession();
        _socket.abort();
        QCoreApplication::exit(code);
    }
    /** 发送一行 JSON 控制消息，写缓冲超限或写入失败时终止驱动。 */
    void send(const QJsonObject &object)
    {
        if (_socket.bytesToWrite() > 65536) { finish(2); return; }
        if (_socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n') < 0) finish(2);
    }
    /** 返回命令状态及当前账号、会话活跃状态和服务端点快照。 */
    void reply(qint64 id, const QString &status)
    {
        send(QJsonObject{{"id", id}, {"status", status}, {"active", _session.isActive()},
                         {"uid", UserMgr::instance()->uid()},
                         {"host", _session.isActive() ? _login.serverHost() : QString()},
                         {"port", _session.isActive() ? _login.serverPort() : 0}});
    }
    /** @brief 返回与指定用户的申请、好友和私聊映射快照。 */
    void friendship(qint64 id, int otherUid)
    {
        const auto user = UserMgr::instance();
        send(QJsonObject{{"id", id}, {"status", "snapshot"}, {"uid", user->uid()},
            {"otherUid", otherUid}, {"applied", user->hasFriendApplication(otherUid)},
            {"friend", user->isFriend(otherUid)}, {"chatId", user->privateChatIdFor(otherUid)}});
    }
    /** 输出有界消息身份、状态及正文摘要快照，不输出正文。 */
    void snapshot(qint64 id, int chatId)
    {
        QJsonArray rows;
        auto *model = _messages.find(chatId);
        if (model && model->rowCount() > 128) { finish(2); return; }
        if (model) for (int i = 0; i < model->rowCount(); ++i) {
            const auto *record = model->recordAt(i);
            rows.append(QJsonObject{{"messageId", QString::number(record->messageId)},
                {"uuid", record->clientMessageId}, {"sender", record->senderId},
                {"status", static_cast<int>(record->deliveryStatus)},
                {"sha256", QString::fromLatin1(QCryptographicHash::hash(record->text.toUtf8(),
                    QCryptographicHash::Sha256).toHex())}});
        }
        send(QJsonObject{{"id", id}, {"status", "snapshot"}, {"chatId", chatId}, {"messages", rows},
            {"more", model ? model->canLoadMore() : true},
            {"cursor", QString::number(model ? model->historyCursor() : 0)}});
    }
    /** @brief 解析控制命令并调用真实客户端业务接口。 */
    void read()
    {
        _input += _socket.readAll();
        if (_input.size() > 8192) { finish(2); return; }
        while (!_stopping && _input.contains('\n')) {
            const auto end = _input.indexOf('\n');
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(_input.left(end), &error);
            _input.remove(0, end + 1);
            if (error.error != QJsonParseError::NoError || !document.isObject()) { finish(2); return; }
            const auto object = document.object();
            const double rawId = object.value("id").toDouble(-1);
            if (rawId <= _lastId || rawId > 9007199254740991.0 || std::floor(rawId) != rawId) {
                finish(2); return;
            }
            _lastId = static_cast<qint64>(rawId);
            const auto command = object.value("command").toString();
            _watchdog.start(60000);
            if (command == "snapshot" && object.size() == 2) {
                reply(_lastId, "snapshot");
            } else if (command == "snapshot" && object.size() == 3 && object.value("chatId").toInt() > 0) {
                snapshot(_lastId, object.value("chatId").toInt());
            } else if (command == "snapshot" && object.size() == 3 && object.value("otherUid").toInt() > 0) {
                friendship(_lastId, object.value("otherUid").toInt());
            } else if ((command == "apply" || command == "accept") && !_pendingId && _session.isActive() &&
                       object.size() == 5 && object.value("toUid").toInt() > 0 &&
                       object.value("description").isString() && object.value("backname").isString()) {
                const auto self = UserMgr::instance()->userInfo();
                const int otherUid = object.value("toUid").toInt();
                const auto description = object.value("description").toString();
                const auto backname = object.value("backname").toString();
                if (!self || description.size() > 255 || backname.size() > 255) { finish(2); return; }
                QByteArray body;
                ReqId request = ID_ADD_FRIEND_REQ;
                if (command == "apply") {
                    body = clientFriendRequest(*self, otherUid, description, backname);
                } else {
                    std::vector<std::shared_ptr<ApplyInfo>> applications;
                    UserMgr::instance()->appendFriendApplicationsTo(applications);
                    for (const auto &apply : applications) {
                        if (apply && apply->_apply_uid == otherUid) {
                            body = clientAcceptFriendRequest(*self, *apply, description, backname);
                            break;
                        }
                    }
                    if (body.isEmpty()) {
                        send(QJsonObject{{"id", _lastId}, {"status", "no-application"}, {"error", -1}});
                        continue;
                    }
                    request = ID_AUTH_FRIEND_REQ;
                }
                if (body.size() > ChatTcpTransport::maxBodyBytes()) { finish(2); return; }
                _pendingId = _lastId;
                _commandError = 0;
                _responsesRemaining = 1;
                _historyRejected = false;
                _expectedResponse = static_cast<int>(request) + 1;
                _commandDeadline.start(10000);
                emit TcpMgr::instance()->sendRequested(request, body);
            } else if ((command == "create" || command == "history" || command == "send") &&
                       !_pendingId && _session.isActive()) {
                QByteArray body;
                ReqId request = ID_CREATE_PRIVATE_CHAT_REQ;
                int copies = 1;
                bool replayCommitted = false;
                const int chatId = object.value("chatId").toInt();
                if (command == "create" && object.size() == 3 && object.value("toUid").toInt() > 0) {
                    body = clientPrivateChatRequest(UserMgr::instance()->uid(), object.value("toUid").toInt());
                } else if (command == "history" && object.size() == 4 && chatId > 0 &&
                           object.value("cursor").isString()) {
                    bool ok = false;
                    const qint64 cursor = object.value("cursor").toString().toLongLong(&ok);
                    if (!ok || cursor < 0) { finish(2); return; }
                    request = ID_LOAD_CHAT_MESSAGE_REQ;
                    _historyRejected = false;
                    body = clientHistoryRequest(chatId, cursor);
                } else if (command == "send" &&
                           (object.size() == 6 || (object.size() == 7 && object.value("copies").toDouble() == 2)) &&
                           chatId > 0 &&
                           object.value("toUid").toInt() > 0 && object.value("text").isString() &&
                           !QUuid(object.value("uuid").toString()).isNull()) {
                    copies = object.contains("copies") ? 2 : 1;
                    request = ID_TEXT_CHAT_MSG_REQ;
                    const auto uuid = object.value("uuid").toString();
                    replayCommitted = _committedUuids.contains(uuid);
                    const auto text = object.value("text").toString();
                    if (text.isEmpty() || text.size() > 1024) { finish(2); return; }
                    body = clientTextRequest(UserMgr::instance()->uid(), object.value("toUid").toInt(),
                        chatId, QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"msg_content", text}}});
                    if (body.size() > ChatTcpTransport::maxBodyBytes()) { finish(2); return; }
                    auto dto = std::make_shared<TextChatData>(uuid, chatId, ChatType::PRIVATE,
                        ChatMessageType::TEXT_TYPE, text, UserMgr::instance()->uid(), QTime::currentTime());
                    _messages.getOrCreate(chatId)->appendMessage(clientMessageRecord(dto));
                } else { finish(2); return; }
                _pendingId = _lastId;
                _commandError = 0;
                _responsesRemaining = 1;
                _historyRejected = false;
                _responsesRemaining = copies;
                _lastChatId = command == "create" ? 0 : chatId;
                _expectedResponse = static_cast<int>(request) + 1;
                _commandDeadline.start(10000);
                // Explicit replay/conflict probes must still reach the real server after local commit.
                // New sends and uncertain reconnect recovery use the production persistent owner.
                if (request == ID_TEXT_CHAT_MSG_REQ && !replayCommitted)
                    UserMgr::instance()->messages()->send(QJsonDocument::fromJson(body).object());
                else for (int copy = 0; copy < copies; ++copy)
                    emit TcpMgr::instance()->sendRequested(request, body);
            } else if ((command == "verify" || command == "register") && !_pendingId && !_session.isActive()) {
                const QUrl gate(object.value("gate").toString());
                if (gate.scheme() != "http" || gate.host() != "127.0.0.1" || gate.port() <= 0 ||
                    gate.port() > 65535 || !gate.userInfo().isEmpty() || !object.value("email").isString() ||
                    (command == "verify" ? object.size() != 4 : object.size() != 7)) {
                    finish(2); return;
                }
                QJsonObject body{{"email", object.value("email")}};
                if (command == "register") {
                    if (!object.value("name").isString() || !object.value("password").isString() ||
                        !object.value("code").isString()) { finish(2); return; }
                    body["user"] = object.value("name");
                    body["passwd"] = xorString(object.value("password").toString());
                    body["confirm"] = body.value("passwd");
                    body["varifycode"] = object.value("code");
                }
                _pendingId = _lastId;
                GateHttpRequest request;
                request.url = gate.resolved(QUrl(command == "verify" ? "/get_varifycode" : "/user_register"));
                request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
                request.flowId = static_cast<quint64>(_pendingId);
                request.module = Modules::REGISTERMOD;
                request.requestId = command == "verify" ? ReqId::ID_GET_VERIFY_CODE : ReqId::ID_REG_USER;
                request.deadlineMs = 5000;
                _accounts.post(request);
            } else if (command == "login" && object.size() == 5 && !_pendingId && !_session.isActive()) {
                const QUrl gate(object.value("gate").toString());
                if (gate.scheme() != "http" || gate.host() != "127.0.0.1" || gate.port() <= 0 ||
                    gate.port() > 65535 || !gate.userInfo().isEmpty() ||
                    !object.value("email").isString() || !object.value("password").isString()) {
                    finish(2); return;
                }
                _pendingId = _lastId;
                gate_url_prefix = gate.toString();
                _login.login(gate, object.value("email").toString(), object.value("password").toString());
            } else if (command == "stop" && object.size() == 2) {
                _stopping = true;
                _pendingId = 0;
                _login.cancel();
                _accounts.reset();
                _session.resetSession(SessionResetReason::Logout);
                TcpMgr::instance()->resetConnection(true);
                reply(_lastId, "stopped");
                _watchdog.start(1000);
                _socket.disconnectFromServer();
            } else {
                finish(2); return;
            }
        }
    }
    AuthFlowCoordinator _auth;
    ClientLoginFlow _login;
    ClientSession _session;
    QLocalSocket _socket;
    QTimer _watchdog;
    QTimer _commandDeadline;
    GateHttpTransport _accounts;
    MessageModelStore _messages;
    QSet<QString> _committedUuids;
    int _lastUid = 0;
    int _lastChatId = 0;
    int _expectedResponse = -1;
    int _responsesRemaining = 1;
    int _commandError = 0;
    bool _historyRejected = false;
    QByteArray _input;
    qint64 _lastId = 0;
    qint64 _pendingId = 0;
    bool _stopping = false;
    bool _finished = false;
};

/** 校验控制管道参数，运行驱动并在 Qt 退出前释放单例。 */
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 2 && args[1] == "--check-storage") {
        // Probe only deployed plugins, even when the build runner still has Qt installed.
        QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
        QTemporaryDir root;
        if (!root.isValid()) return 2;
        try {
            LocalMessageStore store;
            store.open(root.path(), 1);
            return store.cursor(1) == 0 ? 0 : 2;
        } catch (const std::exception &) { return 2; }
    }
    if (args.size() != 3 || args[1] != "--control" || args[2].isEmpty()) return 2;
    int result = 2;
    {
        ClientDriver driver(args[2]);
        result = app.exec();
    }
    // Match the GUI entry point: destroy QObject singletons before Qt and logging.
    TcpMgr::releaseInstance();
    UserMgr::releaseInstance();
    return result;
}
