#include "messageservice.h"
#include "tcpframedecoder.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

// Invoked only by the isolated MySQL/production-ChatServer integration runner.
// Arguments: port, account directory, expected first cursor, expected row count.
/** 运行真实 TCP 与 SQLite 同步探针，以退出码报告游标、数量、回执及期限合同。 */
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 5 && arguments.size() != 6) return 2;
    const bool receipts = arguments.size() == 6 && arguments[5] == "receipts";
    const qint64 expectedCursor = arguments[3].toLongLong();
    const int expectedCount = arguments[4].toInt();
    MessageService service;
    QTcpSocket socket;
    TcpFrameDecoder decoder;
    bool firstRequest = true;
    auto send = /** 将 JSON 编码为生产协议帧并写入测试 TCP 连接。 */ [&socket](quint16 id, const QJsonObject &value) {
        const QByteArray body = QJsonDocument(value).toJson(QJsonDocument::Compact);
        QByteArray frame;
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream << id << quint16(body.size());
        frame.append(body);
        socket.write(frame);
    };
    QObject::connect(&socket, &QTcpSocket::connected, &app, /** 连接成功后发送带可选回执能力的固定测试身份登录。 */ [&] {
        send(1005, QJsonObject{{"uid", 8}, {"token", "fixture-token"},
            {"capabilities", receipts ? QJsonArray{"message_receipts_v1"} : QJsonArray{}}});
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &app, /** 解码入站帧，登录成功后启动存储并分发消息及回执同步结果。 */ [&] {
        for (const auto &frame : decoder.append(socket.readAll())) {
            const auto response = QJsonDocument::fromJson(frame.body).object();
            if (frame.messageId == 1006) {
                if (response["error"].toInt(-1) != 0) { app.exit(3); return; }
                if (receipts && !response["capabilities"].toArray().contains("message_receipts_v1")) {
                    app.exit(9); return;
                }
                service.start(arguments[2], 8, receipts);
                service.registerChat(201);
            } else if (frame.messageId == 1028) service.acceptSyncPage(response);
            else if (frame.messageId == 1030 || frame.messageId == 1033) service.acceptReceiptResponse(response);
        }
    });
    QObject::connect(&service, &MessageService::syncRequested, &app, /** 核对首次请求使用恢复游标后发送增量同步帧。 */ [&](QJsonObject request) {
        if (firstRequest && request["after_id"].toInteger() != expectedCursor) { app.exit(4); return; }
        firstRequest = false;
        send(1027, request);
    });
    QObject::connect(&service, &MessageService::synchronized, &app, /** 消息变化后重新读取所属会话历史。 */ [&](int chatId, qint64) {
        service.loadHistory(chatId);
    });
    QObject::connect(&service, &MessageService::receiptRequested, &app, send);
    QObject::connect(&service, &MessageService::messagesChanged, &app, /** 启用回执场景时在回执变化后重读历史。 */ [&](int chatId) {
        if (receipts) service.loadHistory(chatId);
    });
    QObject::connect(&service, &MessageService::historyLoaded, &app,
        /** 核对历史数量及确认状态，必要时上报已读；全部满足后成功退出。 */ [&](int, qint64, const QVector<StoredMessage> &rows, bool) {
            if (rows.size() != expectedCount) {
                if (!receipts) app.exit(5);
                return;
            }
            QVector<qint64> unread;
            for (const auto &row : rows) {
                if (row.state != StoredMessage::Confirmed || row.messageId <= 0) { app.exit(6); return; }
                if (row.receipt != ReceiptLevel::Read) unread.push_back(row.messageId);
            }
            if (receipts && !unread.isEmpty()) { service.observeRead(201, unread); return; }
            app.exit(0);
        });
    QObject::connect(&service, &MessageService::failed, &app, /** 消息服务失败时以专属失败码退出。 */ [&](int, const QString &) { app.exit(7); });
    QTimer::singleShot(12000, &app, /** 探针总期限到达时以超时码退出。 */ [&] { app.exit(8); });
    socket.connectToHost("127.0.0.1", arguments[1].toUShort());
    const int result = app.exec();
    service.stop();
    socket.abort();
    return result;
}
