#include "tcpmgr.h"
#include <QJsonDocument>
#include <QSet>
#include "logmgr.h"
#include "usermgr.h"
#include "messageservice.h"

namespace {
/** @brief 将现有目录回包转换为本地值，不保存令牌或无关协议字段。 */
QJsonObject directoryResponse(ReqId id, const QJsonObject &response, int self)
{
    QJsonArray contacts, conversations, applications;
    for (const auto &value : response["friend_list"].toArray()) {
        auto row = value.toObject();
        row["id"] = row.value("uid");
        contacts.append(row);
    }
    for (const auto &value : response["apply_list"].toArray()) {
        auto row = value.toObject();
        row["id"] = row.value("fromuid");
        applications.append(row);
    }
    for (const auto &value : response["chat_list"].toArray()) {
        auto row = value.toObject();
        if (row["type"].toString() == "group") {
            row["id"] = row["chat_id"]; row["uid"] = 0; row["name"] = row["group_name"];
            conversations.append(row);
            continue;
        }
        if (row["type"].toString() != "private") continue;
        const int peer = row["user1_id"].toInt() == self ? row["user2_id"].toInt() : row["user1_id"].toInt();
        conversations.append(QJsonObject{{"id", row["chat_id"]}, {"uid", peer}, {"type", "private"}});
    }
    if (id == ID_CREATE_GROUP_RSP || id == ID_GROUP_MANAGE_RSP || id == ID_GROUP_INFO_RSP) {
        auto row = response;
        row.remove("members"); row.remove("request_id"); row.remove("error");
        row["id"] = response["chat_id"]; row["uid"] = 0; row["type"] = "group"; row["name"] = response["group_name"];
        conversations.append(row);
    }
    if (id == ID_FRIEND_REMARK_RSP) {
        contacts.append(QJsonObject{{"id",response["target_uid"]},{"uid",response["target_uid"]},{"backname",response["name"]}});
    }
    int approved = 0;
    if (id == ID_NOTIFY_ADD_FRIEND_REQ) {
        auto row = response;
        row.remove("error");
        row["id"] = row.value("fromuid");
        row["status"] = 0;
        applications.append(row);
    } else if (id == ID_AUTH_FRIEND_RSP || id == ID_NOTIFY_AUTH_FRIEND_REQ) {
        const bool approving = id == ID_AUTH_FRIEND_RSP;
        const auto info = response[approving ? "applyinfo" : "authinfo"].toObject();
        const QString prefix = approving ? "apply" : "auth";
        const int peer = info[prefix + "uid"].toInt();
        const QJsonValue backname = response[approving ? "authinfo" : "applyinfo"].toObject().value("backname");
        contacts.append(QJsonObject{{"id", peer}, {"uid", peer}, {"name", info[prefix + "name"]},
            {"description", info[prefix + "description"]}, {"icon", info[prefix + "icon"]},
            {"sex", info[prefix + "sex"]}, {"backname", backname}});
        conversations.append(QJsonObject{{"id", response["chatid"]}, {"uid", peer}, {"type", "private"}});
        if (approving) approved = peer;
    } else if (id == ID_CREATE_PRIVATE_CHAT_RSP) {
        const auto info = response["other_info"].toObject();
        conversations.append(QJsonObject{{"id", response["chat_id"]}, {"uid", response["other_id"]},
            {"type", "private"}, {"name", info["other_name"]}, {"icon", info["other_icon"]}});
    }
    return {{"contacts", contacts}, {"conversations", conversations},
        {"applications", applications}, {"approved_uid", approved}};
}
}

TcpMgr::TcpMgr() : _host("") {

    // 绑定连接完成信号到lambda槽函数上
    connect(&_transport, &ChatTcpTransport::connected, this,
            /** @brief 连接成功后开启发送入口并通知认证流程。 */
            [this](quint64, quint64) {
        _acceptingSends = true;
        SPDLOG_INFO("connected to chat server");
        emit connectionAttemptFinished(true);
    });

    // 分发传输层已解码的完整消息帧。
    connect(&_transport, &ChatTcpTransport::frameReceived, this,
            /** @brief 将当前连接完整帧交给消息处理器。 */
            [this](const ChatTcpFrame &frame) {
        handleMessage(ReqId(frame.messageId), frame.body.size(), frame.body);
    });

    // 处理错误信号
    connect(&_transport, &ChatTcpTransport::finished, this,
            /** @brief 按终止原因清理发送状态并通知预期或异常关闭。 */
            [this](const ChatTcpOutcome &outcome) {
        const bool expectedClose = _expectedClose
            || outcome.terminal == ChatTcpTerminal::LocalClosed
            || outcome.terminal == ChatTcpTerminal::Reset
            || outcome.terminal == ChatTcpTerminal::Superseded;
        _expectedClose = false;
        _acceptingSends = false;
        _authenticated = false;
        _legacyHistoryRequests.clear();
        if (expectedClose && !_retainingPending) UserMgr::instance()->messages()->pauseOutgoing();
        UserMgr::instance()->messages()->stop();
        if (outcome.terminal == ChatTcpTerminal::Refused
            || outcome.terminal == ChatTcpTerminal::ConnectDeadlineExceeded) {
            emit connectionAttemptFinished(false);
        } else {
            emit connectionClosed(expectedClose);
        }
    });

    // 连接发送数据信号与槽函数
    connect(this, &TcpMgr::sendRequested, this, &TcpMgr::sendData);
    auto *messages = UserMgr::instance()->messages();
    // 将已落盘的发送确认同步到兼容缓存并通知界面。
    connect(messages, &MessageService::sendResponseApplied, this,
        /** @brief 将已落盘 ACK 应用到会话展示并继续失败处理。 */
        [this](const QJsonObject &response) {
        const int chatId = response["chat_id"].toInt();
        const int error = response["error"].toInt(-1);
        if (error == 0) {
            QVector<MessageAcknowledgement> acknowledgements;
            const auto chat = UserMgr::instance()->chatInfo(chatId);
            for (const auto &entry : response["uuid_msgId"].toArray()) {
                const auto item = entry.toObject();
                const auto uuid = item["msg_uuid"].toString();
                const int id = item["message_id"].toInt();
                acknowledgements.push_back({uuid, id});
                if (chat) {
                    const auto cached = chat->getCacheChatMessage(uuid);
                    chat->eraseCacheChatMessage(uuid);
                    if (cached) {
                        cached->setMessageId(id);
                        cached->setStatus(ChatStatus::STATUS_NO_READ);
                        chat->addChatData(cached);
                    }
                }
            }
            emit messagesAcknowledged(chatId, acknowledgements);
        }
        emit requestCompleted(ID_TEXT_CHAT_MSG_RSP, error);
    });
    // 发送消息正文增量同步请求。
    connect(messages, &MessageService::syncRequested, this,
        /** @brief 将消息增量请求编码后发送。 */
        [this](const QJsonObject &request) {
        emit sendRequested(ID_LOAD_CHAT_MESSAGE_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
    });
    // 发送消息服务已调度的持久化批次。
    connect(messages, &MessageService::sendRequested, this,
        /** @brief 将持久化发送批次编码后发送。 */
        [this](const QJsonObject &request) {
        emit sendRequested(ID_TEXT_CHAT_MSG_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
    });

    // 转发回执上报或同步请求。
    connect(messages, &MessageService::receiptRequested, this,
        /** @brief 将回执请求按指定协议 ID 编码后发送。 */
        [this](quint16 id, const QJsonObject &request) {
        emit sendRequested(static_cast<ReqId>(id), QJsonDocument(request).toJson(QJsonDocument::Compact));
    });

    _directoryTimer.setInterval(10000);
    connect(&_directoryTimer, &QTimer::timeout, this,
        /** @brief 上轮目录完整加载后重新扫描，发现其他成员建立的群。 */
        [this] {
            if (!_authenticated) return;
            emit sendRequested(ID_LOAD_CHAT_LIST_REQ, QJsonDocument(QJsonObject{
                {"uid", UserMgr::instance()->uid()}, {"current_chat_id", 0}}).toJson(QJsonDocument::Compact));
        });
    _directoryTimer.start();
    // 注册回调处理逻辑
    initHandlers();
}

void TcpMgr::initHandlers()
{
    for (const auto id : {ID_GROUP_INFO_RSP, ID_GROUP_MANAGE_RSP, ID_FRIEND_REMARK_RSP}) {
        _handlers.insert(id, /** @brief 发布已落盘的业务操作结果。 */ [this](ReqId id, int, QByteArray bytes) {
            emit groupResponse(id, QJsonDocument::fromJson(bytes).object());
        });
    }
    _handlers.insert(ID_CREATE_GROUP_RSP,
        /** @brief 群目录落盘后发布建群结果，失败也携带请求身份。 */
        [this](ReqId, int, QByteArray data) {
            emit groupCreated(QJsonDocument::fromJson(data).object());
        });
    // 登录请求的回包处理逻辑
    _handlers.insert(ReqId::ID_CHAT_LOGIN_RSP,
        /** @brief 解析聊天登录回复并完成身份和初始数据接入。 */
        [this](ReqId id, int len, QByteArray data) {
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
        UserMgr::instance()->setToken(token);
        UserMgr::instance()->setUserInfo(user_info);
        UserMgr::instance()->startResourceSession();
        _authenticated = true;
        const bool receipts = jsonObj["capabilities"].toArray().contains("message_receipts_v1");
        UserMgr::instance()->messages()->start(UserMgr::instance()->storageRoot(), uid, receipts);

        emit loginSucceeded();
        UserMgr::instance()->messages()->saveDirectory(directoryResponse(id, jsonObj, uid),
            /** @brief 首批目录落盘后继续服务端分页，界面分页独立读取本地。 */
            [this, jsonObj, uid] {
                const int cursor = jsonObj["current_chat_id"].toInt();
                const bool more = jsonObj["load_more"].toBool();
                UserMgr::instance()->setChatListCursor(cursor);
                UserMgr::instance()->setChatListFullyLoaded(!more);
                emit chatListLoaded(jsonObj["chat_list"].toArray());
                if (more && cursor > 0) {
                    emit sendRequested(ID_LOAD_CHAT_LIST_REQ, QJsonDocument(QJsonObject{
                        {"uid", uid}, {"current_chat_id", cursor}}).toJson(QJsonDocument::Compact));
                }
            });
    });

    // 搜索用户请求的回包处理逻辑
    _handlers.insert(ReqId::ID_SEARCH_USER_RSP,
        /** @brief 解析用户搜索回复并通知搜索页面。 */
        [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        SPDLOG_DEBUG("received search user response, msg_id={}, payload_size={}",
                     static_cast<int>(id), data.size());

        // 将字节流转换为json
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        emit userSearchResponse(jsonDoc.object());

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
    _handlers.insert(ReqId::ID_ADD_FRIEND_RSP,
        /** @brief 解析好友申请发送结果。 */
        [this](ReqId id, int len, QByteArray data) {
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
    _handlers.insert(ReqId::ID_NOTIFY_ADD_FRIEND_REQ,
        /** @brief 解析对方发来的好友申请通知。 */
        [this](ReqId id, int len, QByteArray data) {
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
    _handlers.insert(ReqId::ID_AUTH_FRIEND_RSP,
        /** @brief 解析好友审批回复并更新联系人。 */
        [this](ReqId id, int len, QByteArray data) {
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

        auto chat_info = UserMgr::instance()->chatInfo(chat_id);
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
            text_msg->setClientMessageId(msg_info["msg_uuid"].toString());
            chat_info->addChatData(text_msg);
        }

        // 发送认证信息
        auto auth_info = std::make_shared<AuthInfo> (authuid, authname, authdescription, authicon, authsex, backname);


        emit friendAdded(auth_info);
        emit friendChatAdded(chat_info);
    });

    // 服务器通知我认证添加好友
    _handlers.insert(ReqId::ID_NOTIFY_AUTH_FRIEND_REQ,
        /** @brief 解析对方完成好友审批的通知。 */
        [this](ReqId id, int len, QByteArray data) {
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

        auto chat_info = UserMgr::instance()->chatInfo(chat_id);
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
            text_msg->setClientMessageId(msg_info["msg_uuid"].toString());
            chat_info->addChatData(text_msg);
        }

        // 发送认证信息
        auto auth_info = std::make_shared<AuthInfo> (authuid, authname, authdescription, authicon, authsex, backname);


        emit friendAdded(auth_info);
        emit friendChatAdded(chat_info);
    });

    // 将发送回包交给消息服务校验并落盘。
    _handlers.insert(ID_TEXT_CHAT_MSG_RSP,
        /** @brief 将文本 ACK 交给持久化消息服务。 */
        [](ReqId, int, QByteArray data) {
        UserMgr::instance()->messages()->acceptSendResponse(QJsonDocument::fromJson(data).object());
    });
    for (const auto id : {ID_MESSAGE_RECEIPT_REPORT_RSP, ID_MESSAGE_RECEIPT_SYNC_RSP}) {
        // 将回执结果交给消息服务合并。
        _handlers.insert(id,
            /** @brief 将回执回复交给持久化消息服务。 */
            [](ReqId, int, QByteArray data) {
            UserMgr::instance()->messages()->acceptReceiptResponse(QJsonDocument::fromJson(data).object());
        });
    }
    // 收到回执变化提示后主动补拉权威状态。
    _handlers.insert(ID_MESSAGE_RECEIPT_CHANGED_NOTIFY,
        /** @brief 收到回执变化提示后登记会话并补拉消息及回执。 */
        [](ReqId, int, QByteArray data) {
        auto *messages = UserMgr::instance()->messages();
        const int chatId = QJsonDocument::fromJson(data).object()["chat_id"].toInt();
        messages->registerChat(chatId);
        messages->synchronizeReceipts(chatId);
    });

    // 服务器通知接收文本聊天数据
    _handlers.insert(ReqId::ID_NOTIFY_CHAT_MSG_REQ,
        /** @brief 解析新消息通知并更新会话及同步状态。 */
        [this](ReqId id, int len, QByteArray data) {
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
        auto chat_info = UserMgr::instance()->chatInfo(chat_id);
        std::vector<std::shared_ptr<ChatDataBase>> msgs;
        for (const auto& msg : json) {
            const auto info = msg.toObject();
            auto msgid = info["message_id"].toInt();
            auto msgcontent = info["msg_content"].toString();
            auto text_msg = std::make_shared<TextChatData> (msgid, chat_id, ChatType::PRIVATE,
                                                           ChatMessageType::TEXT_TYPE, msgcontent,
                                                           from_uid, QTime::currentTime());
            text_msg->setClientMessageId(info["msg_uuid"].toString());
            if (chat_info) {
                chat_info->addChatData(text_msg);
            }
            msgs.push_back(text_msg);
        }

        emit chatMessagesReceived(from_uid, to_uid, chat_id, msgs);
        UserMgr::instance()->messages()->registerChat(chat_id);
        UserMgr::instance()->messages()->synchronize(chat_id);
    });

    // 服务器通知客户端下线
    _handlers.insert(ReqId::ID_NOTIFY_OFF_LINE_REQ,
        /** @brief 解析强制下线通知并结束当前账号会话。 */
        [this](ReqId id, int len, QByteArray data) {
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
    _handlers.insert(ReqId::ID_HEART_BEAT_RSP,
        /** @brief 解析心跳回复并处理在线状态。 */
        [this](ReqId id, int len, QByteArray data) {
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
    _handlers.insert(ReqId::ID_LOAD_CHAT_LIST_RSP,
        /** @brief 解析会话列表页并更新列表模型。 */
        [this](ReqId id, int len, QByteArray data) {
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

        auto self_id = UserMgr::instance()->uid();

        auto current_chat_id = jsonObj["current_chat_id"].toInt();
        auto load_more = jsonObj["load_more"].toBool();

        // 更新状态
        UserMgr::instance()->setChatListCursor(current_chat_id);
        // 传过来的是否还能够加载，因此这里应该取非
        UserMgr::instance()->setChatListFullyLoaded(!load_more);

        const auto chat_list = jsonObj["chat_list"].toArray();
        emit chatListLoaded(chat_list);
        if (load_more && current_chat_id > 0) {
            QJsonObject next{{"uid", self_id}, {"current_chat_id", current_chat_id}};
            emit sendRequested(ID_LOAD_CHAT_LIST_REQ, QJsonDocument(next).toJson(QJsonDocument::Compact));
        }
    });

    // 创建私有聊天请求回包
    _handlers.insert(ReqId::ID_CREATE_PRIVATE_CHAT_RSP,
        /** @brief 解析创建私聊回复并通知进入会话。 */
        [this](ReqId id, int len, QByteArray data) {
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

        auto chat_info = UserMgr::instance()->chatInfo(chat_id);

        emit privateChatCreated(chat_info);
    });

    // 增量拉取聊天记录回包
    _handlers.insert(ReqId::ID_LOAD_CHAT_MESSAGE_RSP,
        /** @brief 区分消息同步与旧历史回复并交给对应处理器。 */
        [this](ReqId id, int len, QByteArray data) {
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
            UserMgr::instance()->messages()->acceptSyncPage(jsonObj);
            return;
        }
        const bool requestedLegacy = _legacyHistoryRequests.remove(jsonObj["chat_id"].toInt());
        if (UserMgr::instance()->messages()->isActive() && !requestedLegacy) {
            // Only explicit legacy reads reach the compatibility model; sync still requires its envelope.
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
            msg_info->setClientMessageId(msg_obj["msg_uuid"].toString());
            if (status >= ChatStatus::STATUS_EMPTY && status <= ChatStatus::STATUS_READ_ALREADY) {
                msg_info->setStatus(static_cast<ChatStatus>(status));
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
    const auto response = QJsonDocument::fromJson(data).object();
    const bool directory = id == ID_GROUP_INFO_RSP || id == ID_GROUP_MANAGE_RSP || id == ID_FRIEND_REMARK_RSP || id == ID_CREATE_GROUP_RSP || id == ID_LOAD_CHAT_LIST_RSP || id == ID_NOTIFY_ADD_FRIEND_REQ
        || id == ID_AUTH_FRIEND_RSP || id == ID_NOTIFY_AUTH_FRIEND_REQ || id == ID_CREATE_PRIVATE_CHAT_RSP;
    if (directory && response["error"].toInt(-1) == 0) {
        UserMgr::instance()->messages()->saveDirectory(directoryResponse(id, response, UserMgr::instance()->uid()),
            /** @brief 落盘后保留既有业务完成通知，不重新进入网络分发。 */
            [this, id, len, data] {
                _handlers[id](id, len, data);
                if (id == ID_AUTH_FRIEND_RSP || id == ID_CREATE_PRIVATE_CHAT_RSP) emit requestCompleted(id, 0);
            },
            /** @brief 已知服务端成功而本地保存失败时向管理界面发布明确的恢复提示。 */
            [this, id, response] {
                if (id != ID_GROUP_MANAGE_RSP && id != ID_GROUP_INFO_RSP && id != ID_FRIEND_REMARK_RSP) return;
                auto result = response; result["local_save_failed"] = true;
                emit groupResponse(id,result);
            });
        return;
    }
    _handlers[id](id, len, data);
    if (id == ID_CREATE_PRIVATE_CHAT_RSP || id == ID_LOAD_CHAT_MESSAGE_RSP ||
        id == ID_ADD_FRIEND_RSP || id == ID_AUTH_FRIEND_RSP) {
        const auto object = QJsonDocument::fromJson(data).object();
        if (id == ID_LOAD_CHAT_MESSAGE_RSP && object.contains("request_id")) return;
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
    _legacyHistoryRequests.clear();
    if (expectedClose) {
        UserMgr::instance()->messages()->pauseOutgoing();
    }
    UserMgr::instance()->messages()->stop();
    _host.clear();
    _port = 0;
    _expectedClose = expectedClose;
    _retainingPending = !expectedClose;
    _transport.reset();
    _retainingPending = false;
}

/** @brief 编码并提交 TCP 数据帧；具体连接及写入失败由传输层处理。 */
void TcpMgr::sendData(ReqId reqId, QByteArray dataBytes)
{
    // 无法提交传输时保留文本批次的待核实状态。
    const auto rejected =
        /** @brief 发送被拒绝时保留文本 UUID 并标记结果不确定。 */
        [&] {
        if (reqId != ID_TEXT_CHAT_MSG_REQ) return;
        const auto request = QJsonDocument::fromJson(dataBytes).object();
        QVector<QString> uuids;
        for (const auto &item : request["text_array"].toArray()) uuids.push_back(item.toObject()["msg_uuid"].toString());
        UserMgr::instance()->messages()->markUncertain(request["chat_id"].toInt(), uuids);
    };
    if (!_acceptingSends || ((reqId == ID_TEXT_CHAT_MSG_REQ || reqId == ID_MESSAGE_RECEIPT_REPORT_REQ
         || reqId == ID_MESSAGE_RECEIPT_SYNC_REQ) && !_authenticated)) {
        rejected();
        return;
    }
    if (reqId == ID_CHAT_LOGIN_REQ) {
        auto request = QJsonDocument::fromJson(dataBytes).object();
        request["capabilities"] = QJsonArray{"message_receipts_v1", "group_membership_v1"};
        dataBytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    }
    if (!_transport.send(static_cast<quint16>(reqId), dataBytes)) {
        rejected();
        return;
    }
    if (reqId == ID_LOAD_CHAT_MESSAGE_REQ) {
        const auto request = QJsonDocument::fromJson(dataBytes).object();
        if (!request.contains("request_id") && request.contains("current_msg_id")
            && request["chat_id"].toInt() > 0) {
            _legacyHistoryRequests.insert(request["chat_id"].toInt());
        }
    }
}

void TcpMgr::connectToServer(ServerInfo si)
{
    SPDLOG_DEBUG("received TCP connect signal");
    resetConnection(false);
    // 尝试连接到服务器
    SPDLOG_INFO("connecting to chat server, host={}, port={}",
                LogMgr::toUtf8(si.Host),
                LogMgr::toUtf8(si.Port));
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
