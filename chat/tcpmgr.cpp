#include "tcpmgr.h"
#include <QJsonDocument>
#include <QSet>
#include "logmgr.h"
#include "usermgr.h"

TcpMgr::TcpMgr() : _host("") {

    // 绑定连接完成信号到lambda槽函数上
    connect(&_transport, &ChatTcpTransport::connected, this,
            [this](quint64, quint64) {
        _acceptingSends = true;
        SPDLOG_INFO("connected to chat server");
        emit sig_tcp_connect_success(true);
    });

    // 绑定socket可读取信号到lambda槽函数上
    connect(&_transport, &ChatTcpTransport::frameReceived, this,
            [this](const ChatTcpFrame &frame) {
        handleMsg(ReqId(frame.messageId), frame.body.size(), frame.body);
    });

    // 处理错误信号
    connect(&_transport, &ChatTcpTransport::finished, this,
            [this](const ChatTcpOutcome &outcome) {
        const bool expectedClose = _expectedClose
            || outcome.terminal == ChatTcpTerminal::LocalClosed
            || outcome.terminal == ChatTcpTerminal::Reset
            || outcome.terminal == ChatTcpTerminal::Superseded;
        _expectedClose = false;
        _acceptingSends = false;
        if (expectedClose && !_retainingPending) {
            _pendingTextBatches.clear();
        }
        if (outcome.terminal == ChatTcpTerminal::Refused
            || outcome.terminal == ChatTcpTerminal::ConnectDeadlineExceeded) {
            emit sig_tcp_connect_success(false);
        } else {
            emit sig_connection_close(expectedClose);
        }
    });

    // 处理断开连接信号
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
        SPDLOG_DEBUG("received chat login response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse chat login response as JSON, msg_id={}",
                        static_cast<int>(id));
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("chat login response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("chat login response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("chat login failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            emit sig_login_failed(err);
            return ;
        }

        auto uid = jsonObj["uid"].toInt();
        // An uncertain send belongs to the account that created it, never to the next login.
        for (qsizetype i = _pendingTextBatches.size(); i > 0; --i) {
            if (_pendingTextBatches[i - 1].senderUid != uid) {
                _pendingTextBatches.removeAt(i - 1);
            }
        }
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
        // Authentication is complete. Retry the original bytes/UUIDs once per successful login.
        const auto pending = _pendingTextBatches;
        for (const auto &batch : pending) {
            _transport.send(static_cast<quint16>(ReqId::ID_TEXT_CHAT_MSG_REQ), batch.payload);
        }
    });

    // 搜索用户请求的回包处理逻辑
    _handlers.insert(ReqId::ID_SEARCH_USER_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received search user response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse search user response as JSON, msg_id={}",
                        static_cast<int>(id));
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("search user response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("search user response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            emit sig_tcp_search_user_finish(nullptr);
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("search user failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
        SPDLOG_DEBUG("received add friend response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse add friend response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("add friend response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("add friend response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("add friend failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            return ;
        }
    });

    // 服务器通知我申请添加好友逻辑
    _handlers.insert(ReqId::ID_NOTIFY_ADD_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received add friend notification, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse add friend notification as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("add friend notification contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("add friend notification is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("add friend notification failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
        SPDLOG_DEBUG("received authorize friend response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse authorize friend response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("authorize friend response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("authorize friend response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("authorize friend failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
            text_msg->SetClientMessageId(msg_info["msg_uuid"].toString());
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
        SPDLOG_DEBUG("received authorize friend notification, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse authorize friend notification as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("authorize friend notification contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("authorize friend notification is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("authorize friend notification failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
            text_msg->SetClientMessageId(msg_info["msg_uuid"].toString());
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
        SPDLOG_DEBUG("received text chat response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse text chat response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("text chat response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("text chat response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        const int responseChatId = jsonObj["chat_id"].toInt();
        QSet<QString> responseIds;
        for (const auto &id : jsonObj["client_msg_uuids"].toArray()) {
            responseIds.insert(id.toString());
        }
        if (responseIds.isEmpty() && err == ErrorCodes::SUCCESS) {
            for (const auto &entry : jsonObj["uuid_msgId"].toArray()) {
                responseIds.insert(entry.toObject()["msg_uuid"].toString());
            }
        }
        responseIds.remove(QString());
        if (err == ErrorCodes::SUCCESS) {
            QSet<QString> acknowledgedIds;
            QSet<qint64> serverIds;
            for (const auto &entry : jsonObj["uuid_msgId"].toArray()) {
                const auto item = entry.toObject();
                const auto uuid = item["msg_uuid"].toString();
                const auto serverId = item["message_id"].toInteger();
                if (uuid.isEmpty() || serverId <= 0 || acknowledgedIds.contains(uuid)
                    || serverIds.contains(serverId)) {
                    return;
                }
                acknowledgedIds.insert(uuid);
                serverIds.insert(serverId);
            }
            // Incomplete/invalid acknowledgements are uncertain, not a terminal confirmation.
            if (acknowledgedIds.isEmpty() || acknowledgedIds != responseIds) {
                return;
            }
        }
        QVector<QString> pendingClientIds;
        for (qsizetype i = 0; i < _pendingTextBatches.size(); ++i) {
            const auto &batch = _pendingTextBatches.at(i);
            const QSet<QString> batchIds(batch.clientMessageIds.begin(), batch.clientMessageIds.end());
            if (batch.chatId == responseChatId && !responseIds.isEmpty() && batchIds == responseIds) {
                pendingClientIds = _pendingTextBatches.at(i).clientMessageIds;
                const auto commitError = jsonObj["commit_error"].toString();
                if (commitError != "StorageUnavailable" && commitError != "DeadlineExceeded") {
                    _pendingTextBatches.removeAt(i);
                }
                break;
            }
        }
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("text chat response failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            emit sig_text_chat_msg_failed(responseChatId, pendingClientIds);
            return ;
        }

        auto from_uid = jsonObj["from_uid"].toInt();
        auto to_uid = jsonObj["to_uid"].toInt();
        auto chat_id = jsonObj["chat_id"].toInt();

        const auto json = jsonObj["uuid_msgId"].toArray();
        auto chat_info = UserMgr::GetInstance()->GetChatInfo(chat_id);

        QVector<MessageAcknowledgement> acknowledgements;

        for (const auto& msg : json) {
            auto info = msg.toObject();
            auto uuid = info["msg_uuid"].toString();
            auto msgid = info["message_id"].toInt();
            if (uuid.isEmpty() || msgid <= 0) {
                continue;
            }
            // ChatInfo 缓存暂时保留给左侧摘要兼容；右侧状态由 Model 独立更新。
            if (chat_info) {
                auto text_msg = chat_info->GetCacheChatMessage(uuid);
                chat_info->EraseCacheChatMessage(uuid);
                if (text_msg) {
                    text_msg->SetMessageId(msgid);
                    text_msg->SetStatus(ChatStatus::STATUS_READ_ALREADY);
                    chat_info->AddChatData(text_msg);
                }
            }
            acknowledgements.push_back({uuid, msgid});
        }

        emit sig_text_chat_msg_rsp_finish(chat_id, acknowledgements);
    });

    // 服务器通知接收文本聊天数据
    _handlers.insert(ReqId::ID_NOTIFY_CHAT_MSG_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received chat message notification, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse chat message notification as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("chat message notification contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("chat message notification is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("chat message notification failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
            text_msg->SetClientMessageId(info["msg_uuid"].toString());
            if (chat_info) {
                chat_info->AddChatData(text_msg);
            }
            msgs.push_back(text_msg);
        }

        emit sig_update_text_chat_msg(from_uid, to_uid, chat_id, msgs);
    });

    // 服务器通知客户端下线
    _handlers.insert(ReqId::ID_NOTIFY_OFF_LINE_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received offline notification, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse offline notification as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("offline notification contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("offline notification is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("offline notification failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            return ;
        }

        emit sig_notify_offline();
    });

    // 心跳检测回包
    _handlers.insert(ReqId::ID_HEART_BEAT_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received heartbeat response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse heartbeat response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("heartbeat response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("heartbeat response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("heartbeat response failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            return ;
        }
    });

    // 从服务器加载一部分聊天列表
    _handlers.insert(ReqId::ID_LOAD_CHAT_LIST_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received load chat list response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse load chat list response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("load chat list response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("load chat list response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("load chat list response failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
        SPDLOG_DEBUG("received create private chat response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse create private chat response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("create private chat response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("create private chat response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("create private chat response failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
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
        SPDLOG_DEBUG("received load chat message response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 字节流转换失败
        if (jsonDoc.isNull()) {
            SPDLOG_WARN("failed to parse load chat message response as JSON, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("load chat message response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("load chat message response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("load chat message response failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            emit sig_tcp_load_chat_msg_failed(jsonObj["chat_id"].toInt());
            return ;
        }

        // 拿取数据
        auto chat_id = jsonObj["chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();
        auto current_msg_id = jsonObj["current_msg_id"].toInt();
        const auto msgs = jsonObj["msgs"].toArray();

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
                                                            content, send_id, dt);
            msg_info->SetClientMessageId(msg_obj["msg_uuid"].toString());
            if (status >= ChatStatus::STATUS_EMPTY && status <= ChatStatus::STATUS_READ_ALREADY) {
                msg_info->SetStatus(static_cast<ChatStatus>(status));
            }
            chat_msgs.push_back(msg_info);
        }

        emit sig_tcp_load_chat_msg_finish(chat_id, chat_msgs, load_more, current_msg_id);
    });
}

// 回包处理函数，根据id调用不同的回调函数
void TcpMgr::handleMsg(ReqId id, int len, QByteArray data)
{
    if (_handlers.find(id) == _handlers.end()) {
        SPDLOG_WARN("no TCP handler registered for msg_id={}", static_cast<int>(id));
        return ;
    }
    _handlers[id](id, len, data);
    if (id == ID_CREATE_PRIVATE_CHAT_RSP || id == ID_LOAD_CHAT_MESSAGE_RSP ||
        id == ID_TEXT_CHAT_MSG_RSP || id == ID_ADD_FRIEND_RSP || id == ID_AUTH_FRIEND_RSP) {
        const auto object = QJsonDocument::fromJson(data).object();
        emit requestCompleted(id, object.value("error").toInt(-1));
    }
}

// 关闭tcp连接
void TcpMgr::CloseConnection()
{
    resetConnection(true);
}

void TcpMgr::beginSession()
{
    _acceptingSends = true;
}

void TcpMgr::resetConnection(bool expectedClose)
{
    _acceptingSends = false;
    if (expectedClose) {
        _pendingTextBatches.clear();
    }
    _host.clear();
    _port = 0;
    _expectedClose = expectedClose;
    _retainingPending = !expectedClose;
    _transport.reset();
    _retainingPending = false;
}

/**
 * @brief TcpMgr::slot_send_data
 * @param reqId
 * @param data
 * 通过_socket发送数据
 */
void TcpMgr::slot_send_data(ReqId reqId, QByteArray dataBytes)
{
    if (!_acceptingSends) {
        return;
    }

    if (reqId == ReqId::ID_TEXT_CHAT_MSG_REQ) {
        const QJsonDocument document = QJsonDocument::fromJson(dataBytes);
        if (document.isObject()) {
            const QJsonObject object = document.object();
            PendingTextBatch batch;
            batch.chatId = object["chat_id"].toInt();
            batch.senderUid = object["from_uid"].toInt();
            batch.payload = dataBytes;
            for (const auto &entry : object["text_array"].toArray()) {
                batch.clientMessageIds.push_back(entry.toObject()["msg_uuid"].toString());
            }
            if (batch.chatId <= 0 || batch.senderUid <= 0
                || batch.senderUid != UserMgr::GetInstance()->GetUid()
                || batch.clientMessageIds.isEmpty() || dataBytes.size() > ChatTcpTransport::MaxBodyBytes()) {
                return;
            }
            bool existing = false;
            for (const auto &pending : _pendingTextBatches) {
                if (pending.senderUid == batch.senderUid && pending.clientMessageIds == batch.clientMessageIds) {
                    if (pending.payload != batch.payload) {
                        emit sig_text_chat_msg_failed(batch.chatId, batch.clientMessageIds);
                        return;
                    }
                    existing = true;
                    break;
                }
            }
            if (!existing) {
                if (_pendingTextBatches.size() >= 128) {
                    emit sig_text_chat_msg_failed(batch.chatId, batch.clientMessageIds);
                    return;
                }
                _pendingTextBatches.enqueue(std::move(batch));
            }
        } else {
            return;
        }
    }

    // A failed write is uncertain: keep its immutable payload for authenticated retry.
    _transport.send(static_cast<quint16>(reqId), dataBytes);
}

/**
 * @brief TcpMgr::slot_tcp_connect
 * @param si
 * 开始进行tcp连接
 */
void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    SPDLOG_DEBUG("received TCP connect signal");
    resetConnection(false);
    // 尝试连接到服务器
    SPDLOG_INFO("connecting to chat server, host={}, port={}",
                LogMgr::ToUtf8(si.Host),
                LogMgr::ToUtf8(si.Port));
    _host = si.Host;
    _port = static_cast<quint16> (si.Port.toUInt());

    ChatTcpEndpoint endpoint;
    endpoint.host = _host;
    endpoint.port = _port;
    endpoint.flowId = ++_transportFlowId;
    endpoint.connectDeadlineMs = 5000;
    endpoint.writeDeadlineMs = 5000;
    _transport.connectTo(endpoint);
}

TcpMgr::~TcpMgr() {
    _transport.reset();
}
