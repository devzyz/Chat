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
        emit sig_connection_close();
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

        // 加载初始化会话列表
        auto self_id = UserMgr::GetInstance()->GetUid();

        auto current_chat_id = jsonObj["current_chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();

        // 更新状态
        UserMgr::GetInstance()->SetCurrentChatId(current_chat_id);
        UserMgr::GetInstance()->SetIsLoadFinish(load_more);

        if (jsonObj.contains("chat_list")) {
            const auto chat_list = jsonObj["chat_list"].toArray();
            for (const auto & chat : chat_list) {
                QJsonObject obj = chat.toObject();

                auto chat_id = obj["chat_id"].toInt();

                auto type = obj["type"].toString();
                if (type == "private") {
                    auto user1_id = obj["user1_id"].toInt();
                    auto user2_id = obj["user2_id"].toInt();

                    // 另一个人的uid
                    auto other_id = (user1_id == self_id) ? user2_id : user1_id;

                    UserMgr::GetInstance()->SetUidToChatId(other_id, chat_id);
                    // 获取到另一个人的uid
                    auto other_info = UserMgr::GetInstance()->GetFriendById(other_id);
                    // 通过对方的uid, 会话id, 当前消息的id来构造ChatInfo
                    auto chat_info = std::make_shared<ChatInfo> (other_id, other_info->_name, other_info->_icon,
                                                                other_info->_backname, chat_id, ChatType::PRIVATE);
                    UserMgr::GetInstance()->AddChatInfo(chat_id, chat_info);
                }else if (type == "group") {
                    // todo 群聊
                }
            }
            emit sig_tcp_load_chat_finish(chat_list);
        }
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

    // 申请添加好友的回包处理逻辑
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

    // 服务器通知我申请添加好友逻辑
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
        int from_uid = jsonObj["fromuid"].toInt();
        QString from_name = jsonObj["applyname"].toString();
        QString from_description = jsonObj["applydescription"].toString();
        QString from_icon = jsonObj["applyicon"].toString();
        int from_sex = jsonObj["applysex"].toInt();
        int touid = jsonObj["touid"].toInt();
        QString description = jsonObj["description"].toString();
        QString backname = jsonObj["backname"].toString();

        auto apply_info = std::make_shared<ApplyInfo> (from_uid, from_name, from_description, from_icon,
                                                      from_sex, 0, touid, description, backname);

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

        // 添加上好友
        // auth_info保存对方的信息，本客户端为认证发出端，因此对方的信息为applyinfo
        auto applyinfo = jsonObj["applyinfo"].toObject();
        auto authinfo = jsonObj["authinfo"].toObject();

        auto authuid = applyinfo["applyuid"].toInt();
        auto authname = applyinfo["applyname"].toString();
        auto authdescription = applyinfo["applydescription"].toString();
        auto authicon = applyinfo["applyicon"].toString();
        auto authsex = applyinfo["applysex"].toInt();
        auto backname = authinfo["backname"].toString(); // 这里我是我给对方的备注

        auto chat_id = jsonObj["chatid"].toInt();

        auto chat_info = std::make_shared<ChatInfo> (authuid, authname, authicon, backname, chat_id, ChatType::PRIVATE);

        // 更新usermgr
        UserMgr::GetInstance()->SetUidToChatId(authuid, chat_id);
        UserMgr::GetInstance()->AddChatInfo(chat_id, chat_info);
        // 更新消息记录
        for (const auto & msg : jsonObj["chat_msgs"].toArray()) {
            const auto msg_info = msg.toObject();
            auto message_id = msg_info["message_id"].toInt();
            auto chat_id = msg_info["chat_id"].toInt();
            auto send_id = msg_info["send_id"].toInt();
            auto recv_id = msg_info["recv_id"].toInt();
            auto content = msg_info["content"].toString();
            auto status = msg_info["status"].toInt();
            auto text_msg = std::make_shared<TextChatData> (message_id, chat_id, ChatType::PRIVATE, ChatMessageType::TEXT_TYPE, content, send_id, QTime::currentTime());
            chat_info->AddChatData(text_msg);
        }

        // 发送认证信息
        auto auth_info = std::make_shared<AuthInfo> (authuid, authname, authdescription, authicon, authsex, backname);

        // 将好友添加上
        UserMgr::GetInstance()->AddFriend(auth_info);

        emit sig_tcp_add_auth_contact_list(auth_info);
        emit sig_tcp_add_auth_chat_list(chat_info);
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

        // 添加上好友
        // auth_info保存对方的信息，本客户端为申请人发出端，因此对方的信息为authinfo
        auto applyinfo = jsonObj["applyinfo"].toObject();
        auto authinfo = jsonObj["authinfo"].toObject();

        auto authuid = authinfo["authuid"].toInt();
        auto authname = authinfo["authname"].toString();
        auto authdescription = authinfo["authdescription"].toString();
        auto authicon = authinfo["authicon"].toString();
        auto authsex = authinfo["authsex"].toInt();
        auto backname = applyinfo["backname"].toString(); // 这里我是我给对方的备注

        auto chat_id = jsonObj["chatid"].toInt();

        auto chat_info = std::make_shared<ChatInfo> (authuid, authname, authicon, backname, chat_id, ChatType::PRIVATE);

        // 更新usermgr
        UserMgr::GetInstance()->SetUidToChatId(authuid, chat_id);
        UserMgr::GetInstance()->AddChatInfo(chat_id, chat_info);
        // 更新消息记录
        for (const auto & msg : jsonObj["chat_msgs"].toArray()) {
            const auto msg_info = msg.toObject();
            auto message_id = msg_info["message_id"].toInt();
            auto chat_id = msg_info["chat_id"].toInt();
            auto send_id = msg_info["send_id"].toInt();
            auto recv_id = msg_info["recv_id"].toInt();
            auto content = msg_info["content"].toString();
            auto status = msg_info["status"].toInt();
            auto text_msg = std::make_shared<TextChatData> (message_id, chat_id, ChatType::PRIVATE,
                                                           ChatMessageType::TEXT_TYPE, content, send_id, QTime::currentTime());
            chat_info->AddChatData(text_msg);
        }

        // 发送认证信息
        auto auth_info = std::make_shared<AuthInfo> (authuid, authname, authdescription, authicon, authsex, backname);

        // 将好友添加上
        UserMgr::GetInstance()->AddFriend(auth_info);

        emit sig_tcp_add_auth_contact_list(auth_info);
        emit sig_tcp_add_auth_chat_list(chat_info);
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

        auto from_uid = jsonObj["from_uid"].toInt();
        auto to_uid = jsonObj["to_uid"].toInt();
        auto chat_id = jsonObj["chat_id"].toInt();

        const auto json = jsonObj["uuid_msgId"].toArray();
        auto chat_info = UserMgr::GetInstance()->GetChatInfo(chat_id);

        std::vector<QString> uuid_set;

        for (const auto& msg : json) {
            auto info = msg.toObject();
            auto uuid = info["msg_uuid"].toString();
            auto msgid = info["message_id"].toInt();
            // 获取内存中的uuid
            auto text_msg = chat_info->GetCacheChatMessage(uuid);
            chat_info->EraseCacheChatMessage(uuid);

            text_msg->SetMessageId(msgid);
            text_msg->SetStatus(ChatStatus::STATUS_READ_ALREADY);
            chat_info->AddChatData(text_msg);

            uuid_set.push_back(uuid);
        }

        emit sig_text_chat_msg_rsp_finish(chat_id, uuid_set);
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

        auto from_uid = jsonObj["from_uid"].toInt();
        auto to_uid = jsonObj["to_uid"].toInt();
        auto chat_id = jsonObj["chat_id"].toInt();

        const auto json = jsonObj["notify_msgs"].toArray();
        auto chat_info = UserMgr::GetInstance()->GetChatInfo(chat_id);
        std::vector<std::shared_ptr<ChatDataBase>> msgs;
        for (const auto& msg : json) {
            const auto info = msg.toObject();
            auto msgid = info["message_id"].toInt();
            auto msgcontent = info["msg_content"].toString();
            auto text_msg = std::make_shared<TextChatData> (msgid, chat_id, ChatType::PRIVATE,
                                                           ChatMessageType::TEXT_TYPE, msgcontent,
                                                           from_uid, QTime::currentTime());
            chat_info->AddChatData(text_msg);
            msgs.push_back(text_msg);
        }

        emit sig_update_text_chat_msg(from_uid, to_uid, chat_id, msgs);
    });

    // 服务器通知客户端下线
    _handlers.insert(ReqId::ID_NOTIFY_OFF_LINE_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_NOTIFY_OFF_LINE_REQ, handle id is " << id << ". data is " << data;

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
            qDebug() << "Notify Off Line Req Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Off Line Req Failed, error is " << err;
            return ;
        }

        emit sig_notify_offline();
    });

    // 心跳检测回包
    _handlers.insert(ReqId::ID_HEART_BEAT_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_HEART_BEAT_RSP, handle id is " << id << ". data is " << data;

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
            qDebug() << "Heart beat Req Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Heart beat Req Failed, error is " << err;
            return ;
        }
    });

    // 从服务器加载一部分聊天列表
    _handlers.insert(ReqId::ID_LOAD_CHAT_LIST_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_LOAD_CHAT_LIST_RSP, handle id is " << id << ". data is " << data;

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
            qDebug() << "Load Chat List Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Load Chat List Failed, error is " << err;
            return ;
        }

        auto self_id = UserMgr::GetInstance()->GetUid();

        auto current_chat_id = jsonObj["current_chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();

        // 更新状态
        UserMgr::GetInstance()->SetCurrentChatId(current_chat_id);
        // 传过来的是否还能够加载，因此这里应该取非
        UserMgr::GetInstance()->SetIsLoadFinish(!load_more);

        const auto chat_list = jsonObj["chat_list"].toArray();
        for (const auto & chat : chat_list) {
            QJsonObject obj = chat.toObject();

            auto chat_id = obj["chat_id"].toInt();

            auto type = obj["type"].toString();
            if (type == "private") {
                auto user1_id = obj["user1_id"].toInt();
                auto user2_id = obj["user2_id"].toInt();

                // 另一个人的uid
                auto other_id = (user1_id == self_id) ? user2_id : user1_id;

                UserMgr::GetInstance()->SetUidToChatId(other_id, chat_id);
                // 获取到另一个人的uid
                auto other_info = UserMgr::GetInstance()->GetFriendById(other_id);
                // 通过对方的uid, 会话id, 当前消息的id来构造ChatInfo
                auto chat_info = std::make_shared<ChatInfo> (other_id, other_info->_name, other_info->_icon,
                                                            other_info->_backname, chat_id, ChatType::PRIVATE);
                UserMgr::GetInstance()->AddChatInfo(chat_id, chat_info);
            }else if (type == "group") {
                // todo 群聊
            }
        }

        emit sig_tcp_load_chat_finish(chat_list);
    });

    // 创建私有聊天请求回包
    _handlers.insert(ReqId::ID_CREATE_PRIVATE_CHAT_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_CREATE_PRIVATE_CHAT_RSP, handle id is " << id << ". data is " << data;

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
            qDebug() << "Create Private Chat Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Create Private Chat Failed, error is " << err;
            return ;
        }

        // 取出数据
        auto self_uid = jsonObj["self_id"].toInt();
        auto other_uid = jsonObj["other_id"].toInt();
        auto chat_id = jsonObj["chat_id"].toInt();
        auto other_info = jsonObj["other_info"].toObject();

        UserMgr::GetInstance()->SetUidToChatId(other_uid, chat_id);
        auto chat_info = std::make_shared<ChatInfo> (other_uid, other_info["other_name"].toString(),
                                                    other_info["other_icon"].toString(), "", chat_id, ChatType::PRIVATE);

        UserMgr::GetInstance()->AddChatInfo(chat_id, chat_info);

        emit sig_create_private_chat_finish(chat_info);
    });

    // 增量拉取聊天记录回包
    _handlers.insert(ReqId::ID_LOAD_CHAT_MESSAGE_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle ID_LOAD_CHAT_MESSAGE_RSP, handle id is " << id << ". data is " << data;

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
            qDebug() << "Load Chat Message Failed, err is Json Parse Err : " << ErrorCodes::ERR_JSON;
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Load Chat Message Failed, error is " << err;
            return ;
        }

        // 拿取数据
        auto chat_id = jsonObj["chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();
        auto current_msg_id = jsonObj["current_msg_id"].toInt();
        const auto msgs = jsonObj["msgs"].toArray();

        auto chat_info = UserMgr::GetInstance()->GetChatInfo(chat_id);
        std::vector<std::shared_ptr<ChatDataBase>> chat_msgs;
        for (const auto &msg : msgs) {
            const auto msg_obj = msg.toObject();
            auto message_id = msg_obj["message_id"].toInt();
            auto send_id = msg_obj["send_id"].toInt();
            auto recv_id = msg_obj["recv_id"].toInt();
            auto content = msg_obj["content"].toString();
            auto status = msg_obj["status"].toInt();
            auto created_at = msg_obj["created_at"].toInteger();
            QDateTime dt = QDateTime::fromSecsSinceEpoch(created_at);

            auto msg_info = std::make_shared<TextChatData> (message_id, chat_id, ChatType::PRIVATE,
                                                           ChatMessageType::TEXT_TYPE,
                                                            content, send_id, dt.time());

            chat_info->AddChatData(msg_info);
            chat_msgs.push_back(msg_info);
        }
        chat_info->SetIsCanLoadMore(load_more);
        chat_info->SetLastMsgId(current_msg_id);

        emit sig_tcp_load_chat_msg_finish(chat_id, chat_msgs);
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

// 关闭tcp连接
void TcpMgr::CloseConnection()
{
    _socket.close();
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
