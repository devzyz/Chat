#include "tcpmgr.h"
#include <QJsonDocument>
#include "usermgr.h"

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
    // 登录请求的回包处理逻辑
    _handlers.insert(ReqId::ID_CHAT_LOGIN_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_CHAT_LOGIN_RSP, handle id is " << id << ". data is " << data;

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
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Login Failed, error is " << err;
            emit sig_login_failed(err);
            return ;
        }

        auto uid = jsonObj["uid"].toInt();
        auto name =jsonObj["name"].toString();
        auto description = jsonObj["description"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();
        auto token = jsonObj["token"].toString();

        auto user_info = std::make_shared<UserInfo> (uid, name, description, icon, sex);
        UserMgr::GetInstance()->SetToken(token);
        UserMgr::GetInstance()->SetInfo(user_info);

        // 如果包含好友申请列表，则添加上
        if (jsonObj.contains("apply_list")) {
            UserMgr::GetInstance()->AddApplyList(jsonObj["apply_list"].toArray());
        }

        // 如果包含好友列表，则添加上
        if (jsonObj.contains("friend_list")) {
            UserMgr::GetInstance()->AddFriendList(jsonObj["friend_list"].toArray());
        }

        emit sig_login_switch_chat();
    });

    // 搜索用户请求的回包处理逻辑
    _handlers.insert(ReqId::ID_SEARCH_USER_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_SEARCH_USER_RSP, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Search user Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Search user Failed, error is " << err;
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        auto uid = jsonObj["uid"].toInt();
        auto name =jsonObj["name"].toString();
        auto description = jsonObj["description"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();

        auto search_info = std::make_shared<SearchInfo> (uid, name, description, icon, sex);

        emit sig_tcp_search_user_finish(search_info);
    });

    // 添加好友的回包处理逻辑
    _handlers.insert(ReqId::ID_ADD_FRIEND_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_ADD_FRIEND_RSP, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Add Friend Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Add Friend Failed, error is " << err;
            return ;
        }
    });

    // 服务器通知我添加好友逻辑
    _handlers.insert(ReqId::ID_NOTIFY_ADD_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_NOTIFY_ADD_FRIEND_REQ, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Notify Add Friend Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Add Friend Failed, error is " << err;
            return ;
        }

        // 申请人的信息
        int from_uid = jsonObj["applyuid"].toInt();
        QString from_name = jsonObj["applyname"].toString();
        QString from_description = jsonObj["applydescription"].toString();
        QString from_icon = jsonObj["applyicon"].toString();
        int from_sex = jsonObj["applysex"].toInt();

        auto apply_info = std::make_shared<ApplyInfo> (from_uid, from_name, from_description, from_icon, from_sex, 0);

        emit sig_tcp_add_friend_apply(apply_info);
    });

    // 服务器认证添加好友逻辑
    _handlers.insert(ReqId::ID_AUTH_FRIEND_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_AUTH_FRIEND_RSP, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Auth Friend Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Auth Friend Failed, error is " << err;
            return ;
        }

        // 认证好友，A->B A申请B为好友，本客户端是B，这里接收的是A的信息
        int from_uid = jsonObj["uid"].toInt();
        QString from_name = jsonObj["name"].toString();
        QString from_description = jsonObj["description"].toString();
        QString from_icon = jsonObj["icon"].toString();
        int from_sex = jsonObj["sex"].toInt();

        auto auth_info = std::make_shared<AuthInfo> (from_uid, from_name, from_description, from_icon, from_sex);

        emit sig_tcp_add_auth_friend(auth_info);
        // 验证完成后，将好友添加上
        UserMgr::GetInstance()->AddFriend(auth_info);
    });

    // 服务器通知我认证添加好友
    _handlers.insert(ReqId::ID_NOTIFY_AUTH_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_NOTIFY_AUTH_FRIEND_REQ, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Notify Auth Friend Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Auth Friend Failed, error is " << err;
            return ;
        }

        // 服务器通知认证好友，A->B A申请B为好友，本客户端为A，B通过好友申请，此时通知本客户端A进行处理
        // 因此这里获取的是B的信息
        int from_uid = jsonObj["uid"].toInt();
        QString from_name = jsonObj["name"].toString();
        QString from_description = jsonObj["description"].toString();
        QString from_icon = jsonObj["icon"].toString();
        int from_sex = jsonObj["sex"].toInt();

        auto auth_info = std::make_shared<AuthInfo> (from_uid, from_name, from_description, from_icon, from_sex);

        emit sig_tcp_notify_auth_friend(auth_info);

        // 验证完成后，将好友添加上
        UserMgr::GetInstance()->AddFriend(auth_info);
    });

    // 发送文本聊天数据请求回包
    _handlers.insert(ReqId::ID_TEXT_CHAT_MSG_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_TEXT_CHAT_MSG_RSP, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Text Chat Msg Rsp Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Text Chat Msg Rsp Failed, error is " << err;
            return ;
        }

        qDebug() << "Text Chat Msg Rsp Success";
    });

    // 服务器通知接收文本聊天数据
    _handlers.insert(ReqId::ID_NOTIFY_CHAT_MSG_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_NOTIFY_CHAT_MSG_REQ, handle id is " << id << ". data is " << data;

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            qDebug() << "JsonObject is empty.";
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            qDebug() << "Notify Chat Msg Req Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Chat Msg Req Failed, error is " << err;
            return ;
        }

        int from_uid = jsonObj["from_uid"].toInt();
        int to_uid = jsonObj["to_uid"].toInt();
        QJsonArray text_array = jsonObj["text_array"].toArray();

        UserMgr::GetInstance()->AddTextChatMsg(from_uid, to_uid, text_array);

        emit sig_update_text_chat_msg(from_uid, to_uid, text_array);
    });
}

// 回包处理函数，根据id调用不同的回调函数
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
void TcpMgr::slot_send_data(ReqId reqId, QByteArray dataBytes)
{
    uint16_t id = reqId;

    // 计算长度
    quint16 len = static_cast<quint16> (dataBytes.size());

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
