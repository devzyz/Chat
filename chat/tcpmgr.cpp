#include "tcpmgr.h"
#include <QJsonDocument>

TcpMgr::TcpMgr() : _host(""), _port(0), _b_recv_pending(false),
    _message_id(0), _message_len(0){

    // 绑定连接完成信号到lambda槽函数上
    connect(&_socket, &QTcpSocket::connected, [&]() {
        qDebug() << "Connected to server";
        emit sig_tcp_connect_success(true);
    });

    // 绑定socket可读取信号到lambda槽函数上
    connect(&_socket, &QTcpSocket::readyRead, [&]() {
        // 通过追加的方式将_socket的缓冲区内可读的信息读取到程序的_buffer缓存内
        _buffer.append(_socket.readAll());

        forever {
            // _buffer是一个字节流缓冲区，不方便直接读取
            // 通过_buffer构造一个stream流，可以通过这个流读取字节流数据
            QDataStream stream(&_buffer, QIODevice::ReadOnly);
            stream.setVersion(QDataStream::Qt_6_0);

            if (!_b_recv_pending) {
                // 如果长度不够头部长度，则返回继续等待接收
                if (_buffer.size() < static_cast<qsizetype> (sizeof(quint16) * 2)) {
                    return ;
                }

                // 从数据中读取出头部id和数据长度len
                stream >> _message_id;
                stream >> _message_len;

                // 将读取的数据从_buffer中删除，stream不需要删除，他会自己移动
                _buffer.remove(0, static_cast<qsizetype> (sizeof(quint16) * 2));
            }

            // 如果当前剩余长度不够其要求的数据长度，则将接下来需要继续读取数据置为true
            if (_buffer.size() < static_cast<qsizetype> (sizeof(char) * _message_len)) {
                _b_recv_pending = true;
                return ;
            }

            // 走到这里代表_buffer内的数据满足长度要求
            _b_recv_pending = false;

            // 先取子串，通过mid函数
            QByteArray messageBody = _buffer.mid(0, _message_len);
            _buffer.remove(0, _message_len);
            qDebug() << "Message ID : " << _message_id << ". Message Len : " << _message_len
                     << ". Message Body : " << messageBody;

            handleMsg(ReqId(_message_id), _message_len, messageBody);
        }
    });

    // 处理错误信号
    connect(&_socket, &QTcpSocket::errorOccurred, [&](QAbstractSocket::SocketError socketError) {
        qDebug() << "Socket error:" << socketError << _socket.errorString();
    });

    // 处理断开连接信号
    connect(&_socket, &QTcpSocket::disconnected, [&]() {
        qDebug() << "Disconnected from server.";
    });

    // 连接发送数据信号与槽函数
    connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);

    // 注册回调处理逻辑
    initHandlers();
}

void TcpMgr::initHandlers()
{
    _handlers.insert(ReqId::ID_CHAT_LOGIN_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Login Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            emit sig_login_failed(ErrorCodes::ERR_JSON);
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Login Failed, error is " << err;
            emit sig_login_failed(err);
            return ;
        }

        emit sig_login_switch_chat();
    });
}

void TcpMgr::handleMsg(ReqId id, int len, QByteArray data)
{
    if (_handlers.find(id) == _handlers.end()) {
        qDebug() << "not found id [" << id << "] to handle";
        return ;
    }
    _handlers[id](id, len, data);
}

/**
 * @brief TcpMgr::slot_send_data
 * @param reqId
 * @param data
 * 通过_socket发送数据
 */
void TcpMgr::slot_send_data(ReqId reqId, QString data)
{
    uint16_t id = reqId;

    // 将字符串转换为utf-8编码的字节流数组，来进行在网络中发送
    QByteArray dataBytes = data.toUtf8();

    // 计算长度
    quint16 len = static_cast<quint16> (data.size());

    // 字节流数组，保存要发送的，id, len, data
    QByteArray sendData;
    // 创建一个字节写入流，绑定sendData
    QDataStream out(&sendData, QIODevice::WriteOnly);

    // 设置数据流采用网络字节序
    out.setByteOrder(QDataStream::BigEndian);

    // 写入id和长度
    out << id << len;

    // 在结尾添加data的字节流
    sendData.append(dataBytes);

    _socket.write(sendData);
}

/**
 * @brief TcpMgr::slot_tcp_connect
 * @param si
 * 开始进行tcp连接
 */
void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    qDebug() << "receive tcp connect signal";
    // 尝试连接到服务器
    qDebug() << "Connecting to server ...";
    _host = si.Host;
    _port = static_cast<quint16> (si.Port.toUInt());

    // 异步连接服务器
    _socket.connectToHost(_host, _port);
}

TcpMgr::~TcpMgr() {

}
