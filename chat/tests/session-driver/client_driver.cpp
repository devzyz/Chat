#include "clientloginflow.h"
#include "clientsession.h"
#include "clientmessage.h"
#include "clientrequests.h"
#include "messagemodelstore.h"
#include "tcpmgr.h"
#include "usermgr.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <QLocalSocket>
#include <QTimer>
#include <cmath>

// The control channel carries commands, never a substitute Chat wire protocol.
class ClientDriver final : public QObject
{
public:
    explicit ClientDriver(const QString &endpoint)
        : _login(_auth), _session(this)
    {
        _socket.setReadBufferSize(8193);
        _watchdog.setSingleShot(true);
        _commandDeadline.setSingleShot(true);
        connect(&_commandDeadline, &QTimer::timeout, this, [this] { finish(2); });
        connect(&_accounts, &GateHttpTransport::finished, this, [this](const GateHttpResult &result) {
            if (!_pendingId || static_cast<quint64>(_pendingId) != result.flowId) return;
            const auto object = QJsonDocument::fromJson(result.body).object();
            send(QJsonObject{{"id", _pendingId}, {"status", "completed"},
                {"error", result.terminal == GateHttpTerminal::Success ? object.value("error").toInt(-1) : -1}});
            _pendingId = 0;
        });
        connect(&_watchdog, &QTimer::timeout, this, [this] { finish(2); });
        connect(&_socket, &QLocalSocket::errorOccurred, this, [this] { finish(2); });
        connect(&_socket, &QLocalSocket::disconnected, this, [this] { finish(_stopping ? 0 : 2); });
        connect(&_socket, &QLocalSocket::connected, this, [this] {
            _watchdog.start(60000);
            send(QJsonObject{{"event", "ready"}, {"pid", QCoreApplication::applicationPid()}, {"format", 1}});
        });
        connect(&_socket, &QLocalSocket::readyRead, this, [this] { read(); });
        connect(&_login, &ClientLoginFlow::authenticated, this, [this](AuthFlowId) {
            const int uid = UserMgr::GetInstance()->GetUid();
            if (_lastUid && _lastUid != uid) _messages = MessageModelStore{};
            _lastUid = uid;
            _session.beginSession();
            reply(_pendingId, "authenticated");
            _pendingId = 0;
        });
        connect(&_login, &ClientLoginFlow::failed, this, [this](AuthFlowId, AuthError error) {
            send(QJsonObject{{"id", _pendingId}, {"status", "login-failed"}, {"error", static_cast<int>(error)}});
            _pendingId = 0;
        });
        connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connection_close, this, [this](bool expected) {
            if (!expected && _session.isActive()) _session.resetSession(SessionResetReason::UnexpectedDisconnect);
        });
        connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_notify_offline, this,
                [this] { _session.resetSession(SessionResetReason::Kicked); });
        const auto tcp = TcpMgr::GetInstance();
        connect(tcp.get(), &TcpMgr::sig_tcp_add_friend_apply, this,
                [](const std::shared_ptr<ApplyInfo> &apply) {
            if (apply) UserMgr::GetInstance()->AddApply(apply->_apply_uid, apply);
        });
        connect(tcp.get(), &TcpMgr::sig_update_text_chat_msg, this,
                [this](int, int, int, std::vector<std::shared_ptr<ChatDataBase>> &messages) {
            for (const auto &message : messages) {
                const auto record = clientMessageRecord(message);
                _messages.getOrCreate(record.chatId)->appendMessage(record);
            }
        });
        connect(tcp.get(), &TcpMgr::sig_tcp_load_chat_msg_finish, this,
                [this](int chatId, std::vector<std::shared_ptr<ChatDataBase>> messages, bool more, qint64 cursor) {
            QVector<MessageRecord> records;
            for (const auto &message : messages) records.push_back(clientMessageRecord(message));
            if (!_messages.applyHistory(chatId, records, more, cursor)) _historyRejected = true;
        });
        connect(tcp.get(), &TcpMgr::sig_text_chat_msg_rsp_finish, this,
                [this](int chatId, QVector<MessageAcknowledgement> acks) { _messages.acknowledge(chatId, acks); });
        connect(tcp.get(), &TcpMgr::sig_text_chat_msg_failed, this,
                [this](int chatId, const QVector<QString> &ids) { _messages.markFailed(chatId, ids); });
        connect(tcp.get(), &TcpMgr::sig_create_private_chat_finish, this,
                [this](const std::shared_ptr<ChatInfo> &chat) { if (chat) _lastChatId = chat->GetChatId(); });
        connect(tcp.get(), &TcpMgr::requestCompleted, this, [this](ReqId id, int error) {
            if (!_pendingId || id != _expectedResponse) return;
            _commandDeadline.stop();
            send(QJsonObject{{"id", _pendingId}, {"status", "completed"},
                {"error", _historyRejected ? -1 : error}, {"chatId", _lastChatId}});
            _pendingId = 0;
            _expectedResponse = -1;
        });
        _watchdog.start(5000);
        _socket.connectToServer(endpoint);
    }

private:
    void finish(int code)
    {
        if (_finished) return;
        _finished = true;
        _pendingId = 0;
        _login.cancel();
        _accounts.reset();
        _session.resetSession(SessionResetReason::Logout);
        TcpMgr::GetInstance()->resetConnection(true);
        UserMgr::GetInstance()->resetSession();
        _socket.abort();
        QCoreApplication::exit(code);
    }
    void send(const QJsonObject &object)
    {
        if (_socket.bytesToWrite() > 65536) { finish(2); return; }
        if (_socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n') < 0) finish(2);
    }
    void reply(qint64 id, const QString &status)
    {
        send(QJsonObject{{"id", id}, {"status", status}, {"active", _session.isActive()},
                         {"uid", UserMgr::GetInstance()->GetUid()},
                         {"host", _session.isActive() ? _login.serverHost() : QString()},
                         {"port", _session.isActive() ? _login.serverPort() : 0}});
    }
    void friendship(qint64 id, int otherUid)
    {
        const auto user = UserMgr::GetInstance();
        send(QJsonObject{{"id", id}, {"status", "snapshot"}, {"uid", user->GetUid()},
            {"otherUid", otherUid}, {"applied", user->AlreadyApplyAddFriend(otherUid)},
            {"friend", user->CheckIsFriendById(otherUid)}, {"chatId", user->GetUidToChatId(otherUid)}});
    }
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
                const auto self = UserMgr::GetInstance()->GetUserInfo();
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
                    UserMgr::GetInstance()->GetApplyList(applications);
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
                if (body.size() > ChatTcpTransport::MaxBodyBytes()) { finish(2); return; }
                _pendingId = _lastId;
                _historyRejected = false;
                _expectedResponse = static_cast<int>(request) + 1;
                _commandDeadline.start(10000);
                emit TcpMgr::GetInstance()->sig_send_data(request, body);
            } else if ((command == "create" || command == "history" || command == "send") &&
                       !_pendingId && _session.isActive()) {
                QByteArray body;
                ReqId request = ID_CREATE_PRIVATE_CHAT_REQ;
                const int chatId = object.value("chatId").toInt();
                if (command == "create" && object.size() == 3 && object.value("toUid").toInt() > 0) {
                    body = clientPrivateChatRequest(UserMgr::GetInstance()->GetUid(), object.value("toUid").toInt());
                } else if (command == "history" && object.size() == 4 && chatId > 0 &&
                           object.value("cursor").isString()) {
                    bool ok = false;
                    const qint64 cursor = object.value("cursor").toString().toLongLong(&ok);
                    if (!ok || cursor < 0) { finish(2); return; }
                    request = ID_LOAD_CHAT_MESSAGE_REQ;
                    _historyRejected = false;
                    body = clientHistoryRequest(chatId, cursor);
                } else if (command == "send" && object.size() == 6 && chatId > 0 &&
                           object.value("toUid").toInt() > 0 && object.value("text").isString() &&
                           !QUuid(object.value("uuid").toString()).isNull()) {
                    request = ID_TEXT_CHAT_MSG_REQ;
                    const auto uuid = object.value("uuid").toString();
                    const auto text = object.value("text").toString();
                    if (text.isEmpty() || text.size() > 1024) { finish(2); return; }
                    body = clientTextRequest(UserMgr::GetInstance()->GetUid(), object.value("toUid").toInt(),
                        chatId, QJsonArray{QJsonObject{{"msg_uuid", uuid}, {"msg_content", text}}});
                    if (body.size() > ChatTcpTransport::MaxBodyBytes()) { finish(2); return; }
                    auto dto = std::make_shared<TextChatData>(uuid, chatId, ChatType::PRIVATE,
                        ChatMessageType::TEXT_TYPE, text, UserMgr::GetInstance()->GetUid(), QTime::currentTime());
                    _messages.getOrCreate(chatId)->appendMessage(clientMessageRecord(dto));
                } else { finish(2); return; }
                _pendingId = _lastId;
                _historyRejected = false;
                _lastChatId = command == "create" ? 0 : chatId;
                _expectedResponse = static_cast<int>(request) + 1;
                _commandDeadline.start(10000);
                emit TcpMgr::GetInstance()->sig_send_data(request, body);
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
                _login.login(gate, object.value("email").toString(), object.value("password").toString());
            } else if (command == "stop" && object.size() == 2) {
                _stopping = true;
                _pendingId = 0;
                _login.cancel();
                _accounts.reset();
                _session.resetSession(SessionResetReason::Logout);
                TcpMgr::GetInstance()->resetConnection(true);
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
    int _lastUid = 0;
    int _lastChatId = 0;
    int _expectedResponse = -1;
    bool _historyRejected = false;
    QByteArray _input;
    qint64 _lastId = 0;
    qint64 _pendingId = 0;
    bool _stopping = false;
    bool _finished = false;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3 || args[1] != "--control" || args[2].isEmpty()) return 2;
    int result = 2;
    {
        ClientDriver driver(args[2]);
        result = app.exec();
    }
    // Match the GUI entry point: destroy QObject singletons before Qt and logging.
    TcpMgr::ReleaseInstance();
    UserMgr::ReleaseInstance();
    return result;
}
