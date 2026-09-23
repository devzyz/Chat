#include "tcpmgr.h"
#include <QJsonDocument>
#include <QSet>
#include "logmgr.h"
#include "usermgr.h"
#include "messageservice.h"

TcpMgr::TcpMgr() : _host("") {

    // 绑定连接完成信号到lambda槽函数上
    connect(&_transport, &ChatTcpTransport::connected, this,
            [this](quint64, quint64) {
        _acceptingSends = true;
        SPDLOG_INFO("connected to chat server");
        emit connectionAttemptFinished(true);
    });

    // 分发传输层已解码的完整消息帧。
    connect(&_transport, &ChatTcpTransport::frameReceived, this,
            [this](const ChatTcpFrame &frame) {
        handleMessage(ReqId(frame.messageId), frame.body.size(), frame.body);
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
        _authenticated = false;
        if (expectedClose && !_retainingPending) UserMgr::GetInstance()->messages()->pauseOutgoing();
        UserMgr::GetInstance()->messages()->stop();
        if (outcome.terminal == ChatTcpTerminal::Refused
            || outcome.terminal == ChatTcpTerminal::ConnectDeadlineExceeded) {
            emit connectionAttemptFinished(false);
        } else {
            emit connectionClosed(expectedClose);
        }
    });

    // 连接发送数据信号与槽函数
    connect(this, &TcpMgr::sendRequested, this, &TcpMgr::sendData);
    auto *messages = UserMgr::GetInstance()->messages();
    // 将已落盘的发送确认同步到兼容缓存并通知界面。
    connect(messages, &MessageService::sendResponseApplied, this, [this](const QJsonObject &response) {
        const int chatId = response["chat_id"].toInt();
        const int error = response["error"].toInt(-1);
        if (error == 0) {
            QVector<MessageAcknowledgement> acknowledgements;
            const auto chat = UserMgr::GetInstance()->chatInfo(chatId);
            for (const auto &entry : response["uuid_msgId"].toArray()) {
                const auto item = entry.toObject();
                const auto uuid = item["msg_uuid"].toString();
                const int id = item["message_id"].toInt();
                acknowledgements.push_back({uuid, id});
                if (chat) {
                    const auto cached = chat->GetCacheChatMessage(uuid);
                    chat->EraseCacheChatMessage(uuid);
                    if (cached) {
                        cached->SetMessageId(id);
                        cached->SetStatus(ChatStatus::STATUS_NO_READ);
                        chat->AddChatData(cached);
                    }
                }
            }
            emit messagesAcknowledged(chatId, acknowledgements);
        }
        emit requestCompleted(ID_TEXT_CHAT_MSG_RSP, error);
    });
    // 发送消息正文增量同步请求。
    connect(messages, &MessageService::syncRequested, this, [this](const QJsonObject &request) {
        emit sendRequested(ID_LOAD_CHAT_MESSAGE_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
    });
    // 发送消息服务已调度的持久化批次。
    connect(messages, &MessageService::sendRequested, this, [this](const QJsonObject &request) {
        emit sendRequested(ID_TEXT_CHAT_MSG_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
    });

    // 转发回执上报或同步请求。
    connect(messages, &MessageService::receiptRequested, this, [this](quint16 id, const QJsonObject &request) {
        emit sendRequested(static_cast<ReqId>(id), QJsonDocument(request).toJson(QJsonDocument::Compact));
    });

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
            emit loginFailed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("chat login response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            emit loginFailed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("chat login response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            emit loginFailed(ErrorCodes::ERR_JSON);
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("chat login failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            emit loginFailed(err);
            return ;
        }

        auto uid = jsonObj["uid"].toInt();
        auto name =jsonObj["name"].toString();
        auto description = jsonObj["description"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();
        auto token = jsonObj["token"].toString();

        auto user_info = std::make_shared<UserInfo> (uid, name, description, icon, sex);
        UserMgr::GetInstance()->setToken(token);
        UserMgr::GetInstance()->setUserInfo(user_info);
        UserMgr::GetInstance()->startResourceSession();
        _authenticated = true;
        const bool receipts = jsonObj["capabilities"].toArray().contains("message_receipts_v1");
        UserMgr::GetInstance()->messages()->start(UserMgr::GetInstance()->storageRoot(), uid, receipts);

        // 如果包含好友申请列表，则添加上
        if (jsonObj.contains("apply_list")) {
            UserMgr::GetInstance()->addFriendApplications(jsonObj["apply_list"].toArray());
        }

        // 如果包含好友列表，则添加上
        if (jsonObj.contains("friend_list")) {
            UserMgr::GetInstance()->addFriends(jsonObj["friend_list"].toArray());
        }

        emit loginSucceeded();

        // 加载初始化会话列表
        auto self_id = UserMgr::GetInstance()->uid();

        auto current_chat_id = jsonObj["current_chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();

        // 更新状态
        UserMgr::GetInstance()->setChatListCursor(current_chat_id);
        UserMgr::GetInstance()->setChatListFullyLoaded(!load_more);

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

                    UserMgr::GetInstance()->addPrivateChatMapping(other_id, chat_id);
                    // 获取到另一个人的uid
                    auto other_info = UserMgr::GetInstance()->friendById(other_id);
                    // 通过对方的uid, 会话id, 当前消息的id来构造ChatInfo
                    auto chat_info = std::make_shared<ChatInfo>(other_id,
                        other_info ? other_info->_name : QString::number(other_id),
                        other_info ? other_info->_icon : QString(),
                        other_info ? other_info->_backname : QString(), chat_id, ChatType::PRIVATE);
                    UserMgr::GetInstance()->addChatInfo(chat_id, chat_info);
                }else if (type == "group") {
                    // todo 群聊
                }
            }
            emit chatListLoaded(chat_list);
            if (load_more && current_chat_id > 0) {
                QJsonObject next{{"uid", self_id}, {"current_chat_id", current_chat_id}};
                emit sendRequested(ID_LOAD_CHAT_LIST_REQ, QJsonDocument(next).toJson(QJsonDocument::Compact));
            }
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
            emit userSearchFinished(nullptr);
            return ;
        }

        // 取到json键值对数据
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.isEmpty()) {
            SPDLOG_WARN("search user response contains an empty JSON object, msg_id={}",
                        static_cast<int>(id));
            emit userSearchFinished(nullptr);
            return ;
        }

        // 如果结果内不包含error键，则说明json不正确
        if (!jsonObj.contains("error")) {
            SPDLOG_WARN("search user response is missing error field, msg_id={}, error={}",
                        static_cast<int>(id), static_cast<int>(ErrorCodes::ERR_JSON));
            emit userSearchFinished(nullptr);
            return ;
        }

        // 取出error键，判断是否为运行正确
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            SPDLOG_WARN("search user failed, msg_id={}, error={}",
                        static_cast<int>(id), err);
            emit userSearchFinished(nullptr);
            return ;
        }

        auto uid = jsonObj["uid"].toInt();
        auto name =jsonObj["name"].toString();
        auto description = jsonObj["description"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();

        auto search_info = std::make_shared<SearchInfo> (uid, name, description, icon, sex);

        emit userSearchFinished(search_info);
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

        emit friendApplicationReceived(apply_info);
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
        UserMgr::GetInstance()->addPrivateChatMapping(authuid, chat_id);
        UserMgr::GetInstance()->addChatInfo(chat_id, chat_info);
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
        UserMgr::GetInstance()->addFriend(auth_info);

        emit friendAdded(auth_info);
        emit friendChatAdded(chat_info);
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
        UserMgr::GetInstance()->addPrivateChatMapping(authuid, chat_id);
        UserMgr::GetInstance()->addChatInfo(chat_id, chat_info);
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
        UserMgr::GetInstance()->addFriend(auth_info);

        emit friendAdded(auth_info);
        emit friendChatAdded(chat_info);
    });

    // 将发送回包交给消息服务校验并落盘。
    _handlers.insert(ID_TEXT_CHAT_MSG_RSP, [](ReqId, int, QByteArray data) {
        UserMgr::GetInstance()->messages()->acceptSendResponse(QJsonDocument::fromJson(data).object());
    });
    for (const auto id : {ID_MESSAGE_RECEIPT_REPORT_RSP, ID_MESSAGE_RECEIPT_SYNC_RSP}) {
        // 将回执结果交给消息服务合并。
        _handlers.insert(id, [](ReqId, int, QByteArray data) {
            UserMgr::GetInstance()->messages()->acceptReceiptResponse(QJsonDocument::fromJson(data).object());
        });
    }
    // 收到回执变化提示后主动补拉权威状态。
    _handlers.insert(ID_MESSAGE_RECEIPT_CHANGED_NOTIFY, [](ReqId, int, QByteArray data) {
        auto *messages = UserMgr::GetInstance()->messages();
        const int chatId = QJsonDocument::fromJson(data).object()["chat_id"].toInt();
        messages->registerChat(chatId);
        messages->synchronizeReceipts(chatId);
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
        auto chat_info = UserMgr::GetInstance()->chatInfo(chat_id);
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

        emit chatMessagesReceived(from_uid, to_uid, chat_id, msgs);
        UserMgr::GetInstance()->messages()->registerChat(chat_id);
        UserMgr::GetInstance()->messages()->synchronize(chat_id);
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

        emit forcedOffline();
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

        auto self_id = UserMgr::GetInstance()->uid();

        auto current_chat_id = jsonObj["current_chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();

        // 更新状态
        UserMgr::GetInstance()->setChatListCursor(current_chat_id);
        // 传过来的是否还能够加载，因此这里应该取非
        UserMgr::GetInstance()->setChatListFullyLoaded(!load_more);

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

                UserMgr::GetInstance()->addPrivateChatMapping(other_id, chat_id);
                // 获取到另一个人的uid
                auto other_info = UserMgr::GetInstance()->friendById(other_id);
                // 通过对方的uid, 会话id, 当前消息的id来构造ChatInfo
                auto chat_info = std::make_shared<ChatInfo> (other_id, other_info->_name, other_info->_icon,
                                                            other_info->_backname, chat_id, ChatType::PRIVATE);
                UserMgr::GetInstance()->addChatInfo(chat_id, chat_info);
            }else if (type == "group") {
                // todo 群聊
            }
        }

        emit chatListLoaded(chat_list);
        if (load_more && current_chat_id > 0) {
            QJsonObject next{{"uid", self_id}, {"current_chat_id", current_chat_id}};
            emit sendRequested(ID_LOAD_CHAT_LIST_REQ, QJsonDocument(next).toJson(QJsonDocument::Compact));
        }
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

        UserMgr::GetInstance()->addPrivateChatMapping(other_uid, chat_id);
        auto chat_info = std::make_shared<ChatInfo> (other_uid, other_info["other_name"].toString(),
                                                    other_info["other_icon"].toString(), "", chat_id, ChatType::PRIVATE);

        UserMgr::GetInstance()->addChatInfo(chat_id, chat_info);

        emit privateChatCreated(chat_info);
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
        if (jsonObj.contains("request_id")) {
            UserMgr::GetInstance()->messages()->acceptSyncPage(jsonObj);
            return;
        }
        if (UserMgr::GetInstance()->messages()->isActive()) {
            // An old server response cannot establish the incremental sync contract.
            emit chatHistoryFailed(jsonObj["chat_id"].toInt());
            return;
        }

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
            emit chatHistoryFailed(jsonObj["chat_id"].toInt());
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

        emit chatHistoryLoaded(chat_id, chat_msgs, load_more, current_msg_id);
    });
}

// 回包处理函数，根据id调用不同的回调函数
void TcpMgr::handleMessage(ReqId id, int len, QByteArray data)
{
    if (_handlers.find(id) == _handlers.end()) {
        SPDLOG_WARN("no TCP handler registered for msg_id={}", static_cast<int>(id));
        return ;
    }
    _handlers[id](id, len, data);
    if (id == ID_CREATE_PRIVATE_CHAT_RSP || id == ID_LOAD_CHAT_MESSAGE_RSP ||
        id == ID_ADD_FRIEND_RSP || id == ID_AUTH_FRIEND_RSP) {
        const auto object = QJsonDocument::fromJson(data).object();
        emit requestCompleted(id, object.value("error").toInt(-1));
    }
}

// 关闭tcp连接
void TcpMgr::closeConnection()
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
    _authenticated = false;
    if (expectedClose) {
        UserMgr::GetInstance()->messages()->pauseOutgoing();
    }
    UserMgr::GetInstance()->messages()->stop();
    _host.clear();
    _port = 0;
    _expectedClose = expectedClose;
    _retainingPending = !expectedClose;
    _transport.reset();
    _retainingPending = false;
}

void TcpMgr::sendData(ReqId reqId, QByteArray dataBytes)
{
    // 无法提交传输时保留文本批次的待核实状态。
    const auto rejected = [&] {
        if (reqId != ID_TEXT_CHAT_MSG_REQ) return;
        const auto request = QJsonDocument::fromJson(dataBytes).object();
        QVector<QString> uuids;
        for (const auto &item : request["text_array"].toArray()) uuids.push_back(item.toObject()["msg_uuid"].toString());
        UserMgr::GetInstance()->messages()->markUncertain(request["chat_id"].toInt(), uuids);
    };
    if (!_acceptingSends || ((reqId == ID_TEXT_CHAT_MSG_REQ || reqId == ID_MESSAGE_RECEIPT_REPORT_REQ
         || reqId == ID_MESSAGE_RECEIPT_SYNC_REQ) && !_authenticated)) {
        rejected();
        return;
    }
    if (reqId == ID_CHAT_LOGIN_REQ) {
        auto request = QJsonDocument::fromJson(dataBytes).object();
        request["capabilities"] = QJsonArray{"message_receipts_v1"};
        dataBytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    }
    if (!_transport.send(static_cast<quint16>(reqId), dataBytes)) rejected();
}

void TcpMgr::connectToServer(ServerInfo si)
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
