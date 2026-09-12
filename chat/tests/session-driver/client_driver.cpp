#include "clientloginflow.h"
#include "clientsession.h"
#include "tcpmgr.h"
#include "usermgr.h"
#include <QCoreApplication>
#include <QJsonDocument>
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
        connect(&_watchdog, &QTimer::timeout, this, [this] { finish(2); });
        connect(&_socket, &QLocalSocket::errorOccurred, this, [this] { finish(2); });
        connect(&_socket, &QLocalSocket::disconnected, this, [this] { finish(_stopping ? 0 : 2); });
        connect(&_socket, &QLocalSocket::connected, this, [this] {
            _watchdog.start(60000);
            send(QJsonObject{{"event", "ready"}, {"pid", QCoreApplication::applicationPid()}, {"format", 1}});
        });
        connect(&_socket, &QLocalSocket::readyRead, this, [this] { read(); });
        connect(&_login, &ClientLoginFlow::authenticated, this, [this](AuthFlowId) {
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
        _watchdog.start(5000);
        _socket.connectToServer(endpoint);
    }

private:
    void finish(int code)
    {
        if (_finished) return;
        _finished = true;
        _login.cancel();
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
                _login.cancel();
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
