#include "AuthenticatedRecipient.h"
#include "../../common/social/SocialDirectory.h"
#include "../../common/message/PrivateSendAccess.h"
#include "../../common/message/MessagePersistence.h"
#include "../../common/message/MessageReceipts.h"
#include "../../common/message/GroupMembership.h"
#include "../../common/resource/ResourceCatalog.h"
#include "LogicSystem.h"
#include "CSession.h"
#include "Const.h"
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include "Data.h"
#include <memory>
#include "MysqlMgr.h"
#include "MessageCommit.h"
#include "HistoryResponse.h"
#include <chrono>

#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "UserSessionDirectory.h"
#include "ChatGrpcClient.h"
#include "LogMgr.h"

namespace {
// 结果拥有序列化数据；只有分发器执行此操作时才向原连接投递回包。
LogicDispatcher::Completion Reply(std::shared_ptr<CSession> session, short id, std::string body) {
    // 仅向原连接投递响应。
    return [session = std::move(session), id, body = std::move(body)] {
        session->Send(body, id);
    };
}
}

void LogicSystem::QueueDatabase(const std::shared_ptr<CSession>& session, ExternalWork work) {
    const int uid = session->AuthenticatedUid();
    std::function<bool()> check;
    if (uid) {
        // 已取得 SQL 名额后才在 Redis 池复核，期间不借数据库连接。
        check = [this, uid, session] {
            return !session->Closing() && _directory->IsCurrent(uid, session->Id()) &&
                   RefreshAuthenticatedSession(*_presence, uid, session);
        };
    }
    // Redis 复核通过后直接使用预留名额，在执行 SQL 前重新检查本地状态。
    QueueRequest(
        ExternalTask::MySQL,
        session,
        [this, session, uid, work = std::move(work)]() -> Completion {
            if (session->Closing() || (uid && !_directory->IsCurrent(uid, session->Id())))
                return {};
            return work();
        },
        std::move(check));
}

void LogicSystem::QueueRequest(
    ExternalTask kind,
    const std::shared_ptr<CSession>& session,
    ExternalWork work,
    std::function<bool()> sql_check) {
    // 未知结果不冒充永久失败，客户端按原 UUID 核实。
    auto failed = [session] {
        session->Close(SessionCloseReason::LogicUnavailable);
    };
    if (SubmitExternal(kind, session, std::move(work), failed, std::move(sql_check)) != LogicSubmitResult::Accepted)
        failed();
}

LogicDispatcher::Completion LogicSystem::ReplyAndNotify(
    std::shared_ptr<CSession> session,
    short id,
    const Json::Value& response,
    ExternalWork route) {
    // 分发线程先确认提交。通知不持有发送者连接，队满时依赖权威同步恢复。
    return [this,
            reply = Reply(std::move(session), id, messaging::CompactJson(response)),
            route = std::move(route)]() mutable {
        reply();
        QueueNotification(std::move(route));
    };
}
LogicSystem::LogicSystem(
    std::shared_ptr<UserSessionDirectory> directory,
    std::shared_ptr<UserPresenceStore> presence,
    std::shared_ptr<DependencyExecutor> dependencies)
    // 把队列消息路由到已登记业务处理器。
    : LogicDispatcher(
          [this](const LogicMessage& message) {
              return Dispatch(message);
          },
          std::move(dependencies)),
      _fun_callbacks(CreateHandlers()), _directory(std::move(directory)), _presence(std::move(presence)) {}

LogicSystem::~LogicSystem() {
    Stop();
}

bool LogicSystem::Dispatch(const LogicMessage& message) {
    SPDLOG_DEBUG("logic recv msg, msg_id={}", message.id);
    if (message.session->Closing())
        return true;
    const auto callback = _fun_callbacks.find(message.id);
    if (callback == _fun_callbacks.end()) {
        return false;
    }
    if (message.id != MSG_CHAT_LOGIN_REQ) {
        const int uid = message.session->AuthenticatedUid();
        if (uid <= 0 || message.received_uid != uid || !_directory->IsCurrent(uid, message.session->Id()))
            return true;
        // 认证与慢 SQL 分开排队，等待期间只占用当前连接的顺序位置。
        QueueRequest(ExternalTask::Redis, message.session, [this, message, uid]() -> Completion {
            if (message.session->Closing() || !RefreshAuthenticatedSession(*_presence, uid, message.session))
                return {};
            // 回到分发线程后才执行原业务入口。
            return [this, message, uid] {
                DispatchAuthenticated(message, uid);
            };
        });
        return true;
    }
    callback->second(message.session, message.id, message.body);
    return true;
}

void LogicSystem::DispatchAuthenticated(const LogicMessage& message, int uid) {
    if (message.session->Closing() || message.session->AuthenticatedUid() != uid ||
        !_directory->IsCurrent(uid, message.session->Id()))
        return;
    Json::Value request;
    Json::Reader reader;
    if (!reader.parse(message.body, request) || !request.isObject())
        return;
    for (const auto* field : {"uid", "fromuid", "authuid", "from_uid", "self_id"}) {
        if (request.isMember(field) && (!request[field].isInt() || request[field].asInt() != uid)) {
            message.session->Close(SessionCloseReason::ProtocolError);
            return;
        }
    }
    _fun_callbacks.at(message.id)(message.session, message.id, message.body);
}

void LogicSystem::QueueLoginData(
    std::shared_ptr<CSession> session,
    int uid,
    std::string credential,
    Json::Value capabilities) {
    // 在数据库线程执行本次请求的同步存取。
    QueueDatabase(
        session,
        [this, session, capabilities = std::move(capabilities), uid, credential = std::move(credential)]() mutable
            -> Completion {
            Json::Value return_value;
            auto user_info = MysqlMgr::GetInstance()->GetUserByUid(uid);
            if (!user_info) {
                return_value["error"] = ErrorCodes::UidInvalid;
                return Reply(session, MSG_CHAT_LOGIN_RSP, messaging::CompactJson(return_value));
            }

            return_value["error"] = ErrorCodes::Success;
            return_value["uid"] = uid;
            return_value["name"] = user_info->_name;
            return_value["description"] = user_info->_description;
            return_value["icon"] = user_info->_icon;
            return_value["email"] = user_info->_email;
            return_value["sex"] = user_info->_sex;
            return_value["token"] = credential;

            bool social = false;
            if (capabilities.isArray())
                for (const auto& value : capabilities)
                    if (value.isString() && value.asString() == "basic_social_v1")
                        social = true;
            // 新客户端通过目录分页加载，登录响应不随好友和申请数量增长。
            if (social) {
                return_value["load_more"] = true;
                return_value["current_chat_id"] = 0;
                return_value["chat_list"] = Json::Value(Json::arrayValue);
            } else {
                // 旧客户端从登录响应读取好友申请和联系人。
                std::vector<std::shared_ptr<ApplyInfo>> apply_list;
                auto b_apply = MysqlMgr::GetInstance()->GetApplyFriendList(uid, apply_list, 0, 10);
                if (b_apply) {
                    for (auto& info : apply_list) {
                        Json::Value apply;
                        apply["fromuid"] = info->_apply_uid;
                        apply["touid"] = info->_to_uid;
                        apply["applyname"] = info->_apply_name;
                        apply["applydescription"] = info->_apply_description;
                        apply["applyicon"] = info->_apply_icon;
                        apply["applysex"] = info->_apply_sex;
                        apply["status"] = info->_status;
                        apply["description"] = info->_description;
                        apply["backname"] = info->_backname;
                        return_value["apply_list"].append(apply);
                    }
                }

                // 同样的，一定会被更新到数据库中，因此如果查不到可以接受
                // 获取好友列表
                std::vector<std::shared_ptr<UserInfo>> friend_list;
                auto b_user = MysqlMgr::GetInstance()->GetFriendList(uid, friend_list);
                if (b_user) {
                    for (auto& info : friend_list) {
                        Json::Value friends;
                        friends["name"] = info->_name;
                        friends["description"] = info->_description;
                        friends["sex"] = info->_sex;
                        friends["icon"] = info->_icon;
                        friends["uid"] = info->_uid;
                        friends["backname"] = info->_backname;
                        return_value["friend_list"].append(friends);
                    }
                }

                // 初始获取一部分会话列表
                {
                    std::vector<std::shared_ptr<ChatInfoBase>> chat_list;

                    // 是否能够加载更多
                    bool load_more = false;
                    int last_load_id = 0;
                    bool success =
                        MysqlMgr::GetInstance()->GetUserChatList(uid, 0, PAGE_SIZE, chat_list, load_more, last_load_id);

                    if (!success) {
                        return_value["error"] = ErrorCodes::UidInvalid;
                        return Reply(session, MSG_CHAT_LOGIN_RSP, messaging::CompactJson(return_value));
                    }

                    return_value["load_more"] = load_more;
                    return_value["current_chat_id"] = last_load_id;

                    for (auto& chat : chat_list) {
                        Json::Value item;

                        item["type"] = chat->_type;
                        item["chat_id"] = chat->_chat_id;

                        if (chat->_type == "private") {
                            auto child = std::dynamic_pointer_cast<PrivateChatInfo>(chat);
                            item["user1_id"] = child->_user1_id;
                            item["user2_id"] = child->_user2_id;
                        } else if (chat->_type == "group") {
                            auto child = std::dynamic_pointer_cast<GroupChatInfo>(chat);
                            item = child->_membership;
                        }

                        return_value["chat_list"].append(item);
                    }
                }
            }
            bool receipts = false;
            if (capabilities.isArray()) {
                for (const auto& capability : capabilities)
                    if (capability.isString() && capability.asString() == "message_receipts_v1")
                        receipts = true;
            }

            return_value["capabilities"] = Json::Value(Json::arrayValue);
            if (receipts)
                return_value["capabilities"].append("message_receipts_v1");
            bool groups = false;
            if (capabilities.isArray())
                for (const auto& value : capabilities)
                    if (value.isString() && value.asString() == "group_membership_v1")
                        groups = true;

            if (groups)
                return_value["capabilities"].append("group_membership_v1");

            if (social)
                return_value["capabilities"].append("basic_social_v1");
            if (messaging::CompactJson(return_value).size() > ResponseBodyLimit(MSG_CHAT_LOGIN_RSP)) {
                return_value = Json::Value(Json::objectValue);
                return_value["error"] = ErrorCodes::Error_Json;
                return_value["login_error"] = "UpgradeRequired";
                return Reply(session, MSG_CHAT_LOGIN_RSP, messaging::CompactJson(return_value));
            }
            // 绑定完成前继续占用本连接的顺序位置。
            return [this,
                    session,
                    uid,
                    credential,
                    receipts,
                    groups,
                    social,
                    response = std::move(return_value)]() mutable {
                if (session->Closing())
                    return;
                auto resume = AwaitCompletion(session);
                try {
                    // 绑定结束后才释放本连接，并按协商能力回复。
                    session->BindAuthenticatedUser(
                        uid,
                        [session, resume, response = std::move(response), receipts, groups, social](
                            SessionBindResult result) mutable {
                            if (result == SessionBindResult::Bound) {
                                session->EnableReceipts(receipts);
                                session->EnableGroups(groups);
                                session->EnableSocial(social);
                            }
                            // strand 提交协商状态后，将业务判定和回包交回连接所属分片。
                            resume([session, result, response = std::move(response)]() mutable {
                                if (result != SessionBindResult::Bound) {
                                    response = Json::Value(Json::objectValue);
                                    response["error"] = ErrorCodes::RPCFailed;
                                }
                                session->Send(messaging::CompactJson(response), MSG_CHAT_LOGIN_RSP);
                            });
                        },
                        credential);
                } catch (...) {
                    resume({});
                    session->Close(SessionCloseReason::LogicUnavailable);
                }
            };
        });
}

std::map<short, MessageHandler> LogicSystem::CreateHandlers() {
    std::map<short, MessageHandler> handlers;
    for (const short request_id : {1042, 1044, 1046}) {
        // 社交请求使用当前认证身份和安全错误信封。
        handlers[request_id] = [this](std::shared_ptr<CSession> session, const short& id, const std::string& body) {
            Json::Value request, response;
            Json::Reader reader;
            response["error"] = ErrorCodes::Error_Json;
            if (reader.parse(body, request) && request.isObject() && request["request_id"].isString() &&
                !request["request_id"].asString().empty() && request["request_id"].asString().size() <= 64) {
                response["request_id"] = request["request_id"];
                if (!session->SupportsSocial())
                    response["social_error"] = "UpgradeRequired";
                else {
                    // 在数据库线程执行本次请求的同步存取。
                    QueueDatabase(
                        session,
                        [session, uid = session->AuthenticatedUid(), request, response, id]() mutable -> Completion {
                            MysqlMgr::GetInstance()->SocialRequest(uid, request, id, response);
                            if (messaging::CompactJson(response).size() > ResponseBodyLimit(id + 1)) {
                                auto key = response["request_id"];
                                response = Json::Value();
                                response["request_id"] = key;
                                response["error"] = ErrorCodes::Error_Json;
                                response["social_error"] = "ResponseTooLarge";
                            }
                            return Reply(session, id + 1, messaging::CompactJson(response));
                        });
                    return;
                }
            }
            session->Send(messaging::CompactJson(response), id + 1);
        };
    }

    for (const auto id : {MSG_GROUP_INFO_REQ, MSG_GROUP_MANAGE_REQ, MSG_FRIEND_REMARK_REQ}) {
        // 从认证会话取得操作身份并返回有界群业务响应。
        handlers[id] = [this](std::shared_ptr<CSession> session, const short& id, const std::string& body) {
            Json::Value request, response;
            Json::Reader reader;
            response["error"] = ErrorCodes::Error_Json;
            if (reader.parse(body, request) && request.isObject() && request["request_id"].isString() &&
                request["request_id"].asString().size() <= 64) {
                if (id == MSG_FRIEND_REMARK_REQ)
                    request["operation"] = "remark";
                else if (request["operation"] == "remark") {
                    session->Send(messaging::CompactJson(response), id + 1);
                    return;
                }
                // 在数据库线程执行本次请求的同步存取。
                QueueDatabase(
                    session,
                    [session, uid = session->AuthenticatedUid(), request, response, id]() mutable -> Completion {
                        MysqlMgr::GetInstance()->GroupRequest(uid, request, id != MSG_GROUP_INFO_REQ, response);
                        return Reply(session, id + 1, messaging::CompactJson(response));
                    });
                return;
            }
            session->Send(messaging::CompactJson(response), id + 1);
        };
    }
    handlers[MSG_CREATE_GROUP_REQ] =
        /** @brief 校验建群参数，成员与建群身份由数据库事务核对。 */
        [this](std::shared_ptr<CSession> session, const short&, const std::string& body) {
            Json::Value request, response;
            Json::Reader reader;
            response["error"] = ErrorCodes::Error_Json;
            if (reader.parse(body, request) && request["name"].isString() && request["request_id"].isString() &&
                request["members"].isArray()) {
                response["request_id"] = request["request_id"].asString().substr(0, 64);
                std::vector<int> members;
                bool valid = request["members"].size() > 0 && request["members"].size() <= 19;
                for (const auto& member : request["members"]) {
                    if (!member.isInt() || member.asInt() <= 0) {
                        valid = false;
                        break;
                    }
                    members.push_back(member.asInt());
                }
                // 在数据库线程执行本次请求的同步存取。
                QueueDatabase(
                    session,
                    [session,
                     uid = session->AuthenticatedUid(),
                     members = std::move(members),
                     request,
                     response,
                     valid]() mutable -> Completion {
                        int chat = 0;
                        if (valid && MysqlMgr::GetInstance()->CreateGroup(
                                         uid,
                                         request["name"].asString(),
                                         request["request_id"].asString(),
                                         members,
                                         chat)) {
                            response["error"] = 0;
                            response["chat_id"] = chat;
                            response["group_name"] = request["name"];
                            Json::Value info_request, info_response;
                            info_request["chat_id"] = chat;
                            info_request["after_uid"] = 0;
                            info_request["request_id"] = request["request_id"];
                            if (MysqlMgr::GetInstance()->GroupRequest(uid, info_request, false, info_response)) {
                                info_response.removeMember("members");
                                response = info_response;
                            }
                        } else
                            response["error"] = ErrorCodes::UidInvalid;
                        return Reply(session, MSG_CREATE_GROUP_RSP, messaging::CompactJson(response));
                    });
                return;
            }
            session->Send(messaging::CompactJson(response), MSG_CREATE_GROUP_RSP);
        };
    for (const auto id : {MSG_MESSAGE_RECEIPT_REPORT_REQ, MSG_MESSAGE_RECEIPT_SYNC_REQ}) {
        // 处理回执上报或同步，并在成功上报后提示对端补拉。
        // 处理回执上报或增量同步并通知另一参与者。
        handlers[id] = [this](std::shared_ptr<CSession> session, const short& request_id, const std::string& body) {
            const bool report = request_id == MSG_MESSAGE_RECEIPT_REPORT_REQ;
            Json::Value request;
            Json::Reader reader;
            Json::Value response;
            response["version"] = 1;
            response["error"] = ErrorCodes::Error_Json;
            try {
                if (!session->SupportsReceipts())
                    throw messaging::ReceiptError("Unauthorized");
                if (!reader.parse(body, request))
                    throw messaging::ReceiptError("InvalidRequest");
                messaging::ValidateReceiptRequest(request, report);
                response["chat_id"] = request["chat_id"];
                response["request_id"] = request["request_id"];
                response["error"] = 0;
            } catch (const messaging::ReceiptError& error) {
                response["receipt_error"] = error.what();
                session->Send(
                    messaging::CompactJson(response),
                    report ? MSG_MESSAGE_RECEIPT_REPORT_RSP : MSG_MESSAGE_RECEIPT_SYNC_RSP);
                return;
            }
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(
                session,
                [this, session, uid = session->AuthenticatedUid(), request, response, report]() mutable -> Completion {
                    int peer = 0;
                    bool success = false;
                    try {
                        success = MysqlMgr::GetInstance()->HandleReceiptRequest(uid, request, report, response, peer);
                        if (!success)
                            response["error"] = ErrorCodes::UidInvalid;
                    } catch (const messaging::ReceiptError& error) {
                        response["receipt_error"] = error.what();
                    }
                    const short response_id = report ? MSG_MESSAGE_RECEIPT_REPORT_RSP : MSG_MESSAGE_RECEIPT_SYNC_RSP;
                    if (!success || !report)
                        return Reply(session, response_id, messaging::CompactJson(response));
                    // 仅对成功提交的回执排入通知队列。
                    return ReplyAndNotify(
                        session,
                        response_id,
                        response,
                        [this, request, response, peer]() -> Completion {
                            Json::Value notification;
                            notification["chat_id"] = request["chat_id"];
                            notification["latest_revision"] = response["latest_revision"];
                            if (auto local = FindAuthorizedRecipient(*_directory, *_presence, peer)) {
                                if (local->SupportsReceipts())
                                    local->Send(
                                        messaging::CompactJson(notification),
                                        MSG_MESSAGE_RECEIPT_CHANGED_NOTIFY);
                                return {};
                            }
                            const auto presence = _presence->Find(peer);
                            if (presence.status != PresenceStatus::Found || !presence.presence)
                                return {};
                            message::ReceiptChangedReq hint;
                            hint.set_uid(peer);
                            hint.set_chat_id(request["chat_id"].asInt());
                            hint.set_revision(messaging::ReceiptRevision(response["latest_revision"]));
                            // 远端通知只在出站池调用。
                            return [server = presence.presence->server_id, hint = std::move(hint)] {
                                const auto result =
                                    ChatGrpcClient::GetInstance()->NotifyMessageReceiptChanged(server, hint);
                                if (result.error() != 0)
                                    SPDLOG_WARN("receipt notification unavailable, chat_id={}", hint.chat_id());
                            };
                        });
                });
        };
    }
    // 处理用户登录请求
    // 解析聊天登录并校验身份及状态服务结果。
    handlers[MSG_CHAT_LOGIN_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            // 解析msg_data对应的json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err) {
                SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
                return;
            }

            int uid = root["uid"].asInt();
            std::string credential = root["token"].asString();
            SPDLOG_INFO("user login request, uid={}", uid);

            // 登录凭据初验在 Redis 池运行，绑定阶段仍原子复核 token。
            QueueRequest(
                ExternalTask::Redis,
                session,
                [this,
                 session,
                 uid,
                 credential = std::move(credential),
                 capabilities = std::move(root["capabilities"])]() mutable -> Completion {
                    if (session->Closing())
                        return {};
                    const auto validation = RedisMgr::GetInstance()->ValidateToken(uid, credential);
                    if (validation != chat_auth::TokenResult::Valid) {
                        if (validation == chat_auth::TokenResult::Unavailable)
                            SPDLOG_WARN("login credential store unavailable, uid={}", uid);
                        Json::Value response;
                        response["error"] = validation == chat_auth::TokenResult::Invalid ? 1011 : 1010;
                        return Reply(session, MSG_CHAT_LOGIN_RSP, messaging::CompactJson(response));
                    }
                    // 排队期间关闭的登录不再加载资料。
                    return [this,
                            session,
                            uid,
                            credential = std::move(credential),
                            capabilities = std::move(capabilities)]() mutable {
                        if (!session->Closing())
                            QueueLoginData(session, uid, std::move(credential), std::move(capabilities));
                    };
                });
        };

    // 处理搜索用户的请求
    // 按查询条件搜索用户并形成响应。
    handlers[MSG_SEARCH_USER_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) -> void {
        // 解析msg_data对应的json数据
        Json::Reader reader;
        Json::Value root;
        auto err = reader.parse(msg_data, root);
        if (!err || !root.isObject()) {
            SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
            return;
        }

        Json::Value return_value;
        if (root.isMember("request_id") && root["request_id"].isString() && root["request_id"].asString().size() <= 64)
            return_value["request_id"] = root["request_id"];
        if (!root["uid_name"].isString() || root["uid_name"].asString().empty() ||
            (root.isMember("request_id") && (!root["request_id"].isString() || root["request_id"].asString().empty() ||
                                             root["request_id"].asString().size() > 64))) {
            return_value["error"] = ErrorCodes::Error_Json;
            session->Send(messaging::CompactJson(return_value), MSG_SEARCH_USER_RSP);
            return;
        }
        auto uid_name = root["uid_name"].asString();
        // 用户可通过uid/name两种类型搜索，当只有数字时，判定通过uid搜索，否则判定为name搜索
        bool b_digit = IsOnlyDigit(uid_name);
        // 在数据库线程执行本次请求的同步存取。
        QueueDatabase(session, [this, session, uid_name, b_digit, return_value]() mutable -> Completion {
            if (b_digit) {
                GetUserByUid(uid_name, return_value);
            } else {
                GetUserByName(uid_name, return_value);
            }
            return Reply(session, MSG_SEARCH_USER_RSP, messaging::CompactJson(return_value));
        });
    };

    // 处理申请好友的请求
    // 校验并保存好友申请，必要时通知对端。
    handlers[MSG_ADD_FRIEND_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            // 解析msg_data对应的json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err) {
                SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
                return;
            }

            auto fromuid = root["fromuid"].asInt(); // 申请人信息
            auto applyname = root["applyname"].asString();
            auto applydescription = root["applydescription"].asString();
            auto applyicon = root["applyicon"].asString();
            auto applysex = root["applysex"].asInt();
            auto touid = root["touid"].asInt();                // 申请添加touid为好友
            auto description = root["description"].asString(); // 申请信息
            auto backname = root["backname"].asString();       // 申请人给touid的备注名

            SPDLOG_DEBUG(
                "add friend request, fromuid={}, touid={}, description_size={}, backname_size={}",
                fromuid,
                touid,
                description.size(),
                backname.size());

            Json::Value return_value;
            return_value["error"] = ErrorCodes::Success;
            // 先更新数据库

            // Identity belongs to the authenticated connection, not the request body.
            if (session->AuthenticatedUid() <= 0 || fromuid != session->AuthenticatedUid() || touid <= 0 ||
                fromuid == touid) {
                return_value["error"] = ErrorCodes::UidInvalid;
                session->Send(messaging::CompactJson(return_value), MSG_ADD_FRIEND_RSP);
                return;
            }
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(
                session,
                [this,
                 session,
                 fromuid,
                 touid,
                 description,
                 backname,
                 applyname,
                 applydescription,
                 applyicon,
                 applysex,
                 return_value]() mutable -> Completion {
                    if (!MysqlMgr::GetInstance()->GetUserByUid(touid)) {
                        return_value["error"] = ErrorCodes::UidInvalid;
                        return Reply(session, MSG_ADD_FRIEND_RSP, messaging::CompactJson(return_value));
                    }
                    bool success = MysqlMgr::GetInstance()->AddFriendApply(fromuid, touid, description, backname);
                    if (!success) {
                        return_value["error"] = ErrorCodes::UidInvalid;
                        return Reply(session, MSG_ADD_FRIEND_RSP, messaging::CompactJson(return_value));
                    }
                    // 只把通知所需的公开资料交给通知队列。
                    auto user_info = MysqlMgr::GetInstance()->GetUserByUid(fromuid);
                    AddFriendReq req;
                    req.set_applyuid(fromuid);
                    req.set_touid(touid);
                    req.set_description(description);
                    req.set_backname(backname);
                    if (user_info) {
                        req.set_applyname(user_info->_name);
                        req.set_applydescription(user_info->_description);
                        req.set_applyicon(user_info->_icon);
                        req.set_applysex(user_info->_sex);
                    }
                    // 提交后确认发送方；Redis 池查找接收者，远端投递交给出站 gRPC 池。
                    return ReplyAndNotify(
                        session,
                        MSG_ADD_FRIEND_RSP,
                        return_value,
                        [this,
                         fromuid,
                         touid,
                         description,
                         backname,
                         applyname,
                         applydescription,
                         applyicon,
                         applysex,
                         req = std::move(req)]() -> Completion {
                            std::string to_ip_value = "";
                            const auto presence = _presence->Find(touid);
                            if (presence.status == PresenceStatus::Unavailable) {
                                SPDLOG_WARN("presence lookup unavailable");
                                return {};
                            }
                            bool b_to_ip = presence.status == PresenceStatus::Found;
                            if (presence.presence)
                                to_ip_value = presence.presence->server_id;
                            if (!b_to_ip) {
                                return {};
                            }

                            // 接下来判断是否跟自己在同一份服务器
                            auto& configMgr = ConfigMgr::GetInstance();
                            auto self_name = configMgr["SelfServer"]["Name"];

                            // 查询到在同一服务器
                            if (to_ip_value == self_name) {
                                auto sessions = _directory;
                                auto touid_session = FindAuthorizedRecipient(*sessions, *_presence, touid);
                                if (touid_session) {
                                    // 直接通知对方
                                    Json::Value notify;
                                    notify["error"] = ErrorCodes::Success;
                                    notify["fromuid"] = fromuid;
                                    notify["applyname"] = applyname;
                                    notify["applydescription"] = applydescription;
                                    notify["applyicon"] = applyicon;
                                    notify["applysex"] = applysex;
                                    notify["touid"] = touid;
                                    notify["description"] = description;
                                    notify["backname"] = backname;
                                    std::string notity_str = notify.toStyledString();
                                    touid_session->Send({MSG_NOTIFY_ADD_FRIEND_REQ, notity_str});
                                }
                                return {};
                            }
                            // 发送grpc请求
                            // 将路由结果和请求所有权交给出站池。
                            return [server = to_ip_value, request = std::move(req)] {
                                ChatGrpcClient::GetInstance()->NotifyOtherAddFriend(server, request);
                            };
                        });
                });
        };

    // 处理认证好友的请求
    // 校验并提交好友审批与会话建立。
    handlers[MSG_AUTH_FRIEND_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            SPDLOG_DEBUG("auth friend request, msg_id={}", static_cast<int>(MSG_AUTH_FRIEND_REQ));
            // 解析json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err) {
                SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
                return;
            }

            auto applyuid = root["applyuid"].asInt(); // 申请人uid
            auto authuid = root["authuid"].asInt();   // 被申请人uid
            auto applyinfo = root["applyinfo"];       // 申请人信息
            auto authinfo = root["authinfo"];         // 认证人信息

            // 返回值
            Json::Value return_value;
            return_value["error"] = ErrorCodes::Success;
            return_value["applyuid"] = applyuid;
            return_value["authuid"] = authuid;
            return_value["applyinfo"] = applyinfo;
            return_value["authinfo"] = authinfo;

            if (session->AuthenticatedUid() <= 0 || authuid != session->AuthenticatedUid() || applyuid <= 0 ||
                applyuid == authuid || !applyinfo.isObject() || !authinfo.isObject() ||
                !applyinfo["applyuid"].isInt() || applyinfo["applyuid"].asInt() != applyuid ||
                !applyinfo["touid"].isInt() || applyinfo["touid"].asInt() != authuid || !authinfo["authuid"].isInt() ||
                authinfo["authuid"].asInt() != authuid || !authinfo["touid"].isInt() ||
                authinfo["touid"].asInt() != applyuid) {
                return_value["error"] = ErrorCodes::UidInvalid;
                session->Send(messaging::CompactJson(return_value), MSG_AUTH_FRIEND_RSP);
                return;
            }
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(
                session,
                [this, session, applyuid, authuid, applyinfo, authinfo, return_value]() mutable -> Completion {
                    std::vector<std::shared_ptr<ChatMessage>> _chat_msgs;
                    int chat_id = 0;
                    SPDLOG_DEBUG(
                        "auth friend description received, applyuid={}, authuid={}, description_size={}",
                        applyuid,
                        authuid,
                        authinfo["description"].asString().size());
                    // 更新数据库
                    bool success = MysqlMgr::GetInstance()->AuthFriendApply(
                        applyuid,
                        authuid,
                        applyinfo["backname"].asString(),
                        authinfo["backname"].asString(),
                        applyinfo["description"].asString(),
                        authinfo["description"].asString(),
                        _chat_msgs,
                        chat_id);

                    return_value["chatid"] = chat_id;

                    if (!success) {
                        return_value["error"] = ErrorCodes::UidInvalid;
                        return Reply(session, MSG_AUTH_FRIEND_RSP, messaging::CompactJson(return_value));
                    }

                    for (auto& msg : _chat_msgs) {
                        Json::Value msg_info;
                        msg_info["message_id"] = msg->_message_id;
                        msg_info["msg_uuid"] = msg->_client_msg_id;
                        msg_info["chat_id"] = msg->_chat_id;
                        msg_info["send_id"] = msg->_send_id;
                        msg_info["recv_id"] = msg->_recv_id;
                        msg_info["content"] = msg->_content;
                        msg_info["status"] = msg->_status;
                        return_value["chat_msgs"].append(msg_info);
                    }
                    // 提交后确认发送方；Redis 池查找接收者，远端投递交给出站 gRPC 池。
                    return ReplyAndNotify(
                        session,
                        MSG_AUTH_FRIEND_RSP,
                        return_value,
                        [this,
                         applyuid,
                         authuid,
                         applyinfo,
                         authinfo,
                         return_value,
                         chat_id,
                         _chat_msgs = std::move(_chat_msgs)]() -> Completion {
                            std::string applyuid_ip_value = "";
                            const auto presence = _presence->Find(applyuid);
                            if (presence.status == PresenceStatus::Unavailable) {
                                SPDLOG_WARN("presence lookup unavailable");
                                return {};
                            }
                            bool isSuccess = presence.status == PresenceStatus::Found;
                            if (presence.presence)
                                applyuid_ip_value = presence.presence->server_id;
                            // 查询不到，说明对方已离线，则不对界面进行更新。因为已经对数据库更新了，因此下次登录是正确结果
                            if (!isSuccess) {
                                return {};
                            }

                            // 拿出自己的server_ip查看是否相同
                            auto& configMgr = ConfigMgr::GetInstance();
                            auto self_server_name = configMgr["SelfServer"]["Name"];

                            // 两个人在同一个服务器上，则直接找到对方的session，并发送请求
                            if (self_server_name == applyuid_ip_value) {
                                auto sessions = _directory;
                                auto session = FindAuthorizedRecipient(*sessions, *_presence, applyuid);
                                if (session) {
                                    Json::Value notify;
                                    notify["error"] = ErrorCodes::Success;
                                    notify["applyuid"] = applyuid;
                                    notify["authuid"] = authuid;
                                    notify["applyinfo"] = applyinfo;
                                    notify["authinfo"] = authinfo;
                                    notify["chatid"] = chat_id;

                                    for (auto& msg : return_value["chat_msgs"]) {
                                        notify["chat_msgs"].append(msg);
                                    }

                                    // 通过session发送
                                    std::string notify_str = messaging::BoundedNotification(notify);
                                    session->Send({MSG_NOTIFY_AUTH_FRIEND_REQ, notify_str});
                                }

                                return {};
                            }

                            // 两个人不在同一个服务器上，通过grpc发送请求
                            AuthFriendReq auth_req;
                            auth_req.set_applyuid(applyuid);
                            auth_req.set_authuid(authuid);
                            auth_req.set_chatid(chat_id);

                            auto message_applyinfo = auth_req.mutable_applyinfo();
                            message_applyinfo->set_applyuid(applyinfo["applyuid"].asInt());
                            message_applyinfo->set_applyname(applyinfo["applyname"].asString());
                            message_applyinfo->set_applydescription(applyinfo["applydescription"].asString());
                            message_applyinfo->set_applyicon(applyinfo["applyicon"].asString());
                            message_applyinfo->set_applysex(applyinfo["applysex"].asInt());
                            message_applyinfo->set_status(applyinfo["status"].asInt());
                            message_applyinfo->set_touid(applyinfo["touid"].asInt());
                            message_applyinfo->set_description(applyinfo["description"].asString());
                            message_applyinfo->set_backname(applyinfo["backname"].asString());

                            auto message_authinfo = auth_req.mutable_authinfo();
                            message_authinfo->set_authuid(authinfo["authuid"].asInt());
                            message_authinfo->set_authname(authinfo["authname"].asString());
                            message_authinfo->set_authdescription(authinfo["authdescription"].asString());
                            message_authinfo->set_authicon(authinfo["authicon"].asString());
                            message_authinfo->set_authsex(authinfo["authsex"].asInt());
                            message_authinfo->set_status(authinfo["status"].asInt());
                            message_authinfo->set_touid(authinfo["touid"].asInt());
                            message_authinfo->set_description(authinfo["description"].asString());
                            message_authinfo->set_backname(authinfo["backname"].asString());

                            for (auto& msg : _chat_msgs) {
                                auto* message_chat_message = auth_req.add_chatmessage();
                                message_chat_message->set_messageid(msg->_message_id);
                                message_chat_message->set_chatid(msg->_chat_id);
                                message_chat_message->set_sendid(msg->_send_id);
                                message_chat_message->set_recvid(msg->_recv_id);
                                message_chat_message->set_content(msg->_content);
                                message_chat_message->set_status(msg->_status);
                                message_chat_message->set_client_msg_uuid(msg->_client_msg_id);
                            }

                            // 将路由结果和请求所有权交给出站池。
                            return [server = applyuid_ip_value, request = std::move(auth_req)] {
                                ChatGrpcClient::GetInstance()->NotifyOtherAuthFriend(server, request);
                            };
                        });
                });
        };

    // 处理用户发送的文本请求
    // 校验并提交文本批次，保留 UUID 幂等身份。
    handlers[MSG_TEXT_CHAT_MSG_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            SPDLOG_DEBUG("text chat message request, msg_id={}", static_cast<int>(MSG_TEXT_CHAT_MSG_REQ));
            // 解析json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err || !root.isObject()) {
                Json::Value invalid;
                invalid["error"] = ErrorCodes::Error_Json;
                invalid["commit_error"] = "InvalidUuid";
                session->Send(invalid.toStyledString(), MSG_TEXT_CHAT_MSG_RSP);
                return;
            }

            const int principal_uid = session->AuthenticatedUid();
            const int from_uid = root["from_uid"].isInt() ? root["from_uid"].asInt() : 0;
            const int to_uid = root["to_uid"].isInt() ? root["to_uid"].asInt() : 0;
            const int chat_id = root["chat_id"].isInt() ? root["chat_id"].asInt() : 0;
            Json::Value data_array = root["text_array"];
            const bool resource_message = root.isMember("resource_id");
            const bool group_message = root["chat_type"].isString() && root["chat_type"].asString() == "group";

            Json::Value return_value;
            if (root["attempt_id"].isString() && root["attempt_id"].asString().size() <= 20)
                return_value["attempt_id"] = root["attempt_id"];

            return_value["error"] = ErrorCodes::Success;
            return_value["from_uid"] = from_uid;
            return_value["to_uid"] = to_uid;
            return_value["chat_id"] = chat_id;

            return_value["client_msg_uuids"] = Json::Value(Json::arrayValue);
            if (data_array.isArray()) {
                for (const auto& msg : data_array) {
                    if (msg.isObject() && msg["msg_uuid"].isString()) {
                        return_value["client_msg_uuids"].append(msg["msg_uuid"]);
                    }
                }
            }
            if (principal_uid <= 0 || principal_uid != from_uid) {
                return_value["error"] = ErrorCodes::UidInvalid;
                return_value["commit_error"] = "UnauthorizedSender";
                session->Send(messaging::CompactJson(return_value), MSG_TEXT_CHAT_MSG_RSP);
                return;
            }
            if (!data_array.isArray() || data_array.empty() || chat_id <= 0 ||
                (group_message ? to_uid != 0 : to_uid <= 0)) {
                return_value["error"] = ErrorCodes::Error_Json;
                return_value["commit_error"] = "InvalidMembership";
                session->Send(messaging::CompactJson(return_value), MSG_TEXT_CHAT_MSG_RSP);
                return;
            }
            std::int64_t relationship = 0;
            if (!group_message && root.isMember("relationship_revision")) {
                try {
                    relationship = social::Number(root["relationship_revision"]);
                } catch (const std::exception&) {
                    return_value["error"] = ErrorCodes::UidInvalid;
                    return_value["commit_error"] = "InvalidMembership";
                    session->Send(messaging::CompactJson(return_value), MSG_TEXT_CHAT_MSG_RSP);
                    return;
                }
            }
            std::int64_t group_epoch = 0;
            if (group_message) {
                try {
                    if (!session->SupportsGroups())
                        throw messaging::GroupError("UpgradeRequired");
                    group_epoch = messaging::GroupNumber(root["membership_epoch"]);
                    return_value["membership_epoch"] = root["membership_epoch"];
                } catch (const std::exception&) {
                    return_value["error"] = ErrorCodes::UidInvalid;
                    return_value["commit_error"] = "InvalidMembership";
                    session->Send(messaging::CompactJson(return_value), MSG_TEXT_CHAT_MSG_RSP);
                    return;
                }
            }
            std::vector<std::pair<std::string, std::string>> _cache_msgs;
            for (auto& msg : data_array) {
                if (!msg.isObject() || !msg["msg_content"].isString() || !msg["msg_uuid"].isString()) {
                    return_value["error"] = ErrorCodes::Error_Json;
                    return_value["commit_error"] = "InvalidUuid";
                    session->Send(messaging::CompactJson(return_value), MSG_TEXT_CHAT_MSG_RSP);
                    return;
                }
                auto msg_content = msg["msg_content"].asString();
                auto msg_uuid = msg["msg_uuid"].asString();
                _cache_msgs.push_back({msg_uuid, msg_content});
            }

            Json::Value ack_budget = return_value;
            for (const auto& item : _cache_msgs) {
                Json::Value mapping;
                mapping["msg_uuid"] = item.first;
                mapping["message_id"] = 2147483647;
                ack_budget["uuid_msgId"].append(mapping);
            }
            if (messaging::CompactJson(ack_budget).size() > ResponseBodyLimit(MSG_TEXT_CHAT_MSG_RSP)) {
                return_value["error"] = ErrorCodes::Error_Json;
                return_value["commit_error"] = "InvalidUuid";
                session->Send(messaging::CompactJson(return_value), MSG_TEXT_CHAT_MSG_RSP);
                return;
            }
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(
                session,
                [this,
                 session,
                 from_uid,
                 to_uid,
                 principal_uid,
                 chat_id,
                 group_epoch,
                 relationship,
                 root,
                 data_array = std::move(data_array),
                 resource_message,
                 group_message,
                 _cache_msgs = std::move(_cache_msgs),
                 return_value]() mutable -> Completion {
                    std::vector<std::shared_ptr<ChatMessage>> _chat_msgs;
                    bool resource_existing = false;
                    message_commit::Result result;
                    if (resource_message) {
                        try {
                            auto sessions = _directory;
                            if (!sessions->IsCurrent(from_uid, session->Id()) || data_array.size() != 1 ||
                                !root["resource_id"].isString() ||
                                !message_commit::IsCanonicalUuid(data_array[0]["msg_uuid"].asString()))
                                throw std::runtime_error("invalid resource sender");
                            auto& config = ConfigMgr::GetInstance();
                            static resource::ResourceCatalog catalog(
                                config["Mysql"]["Host"] + ":" + config["Mysql"]["Port"],
                                config["Mysql"]["User"],
                                config["Mysql"]["Password"],
                                config["Mysql"]["Schema"]);
                            auto saved = catalog.CommitMessage(
                                from_uid,
                                to_uid,
                                chat_id,
                                data_array[0]["msg_uuid"].asString(),
                                root["resource_id"].asString(),
                                group_epoch,
                                relationship);
                            resource_existing = saved.existing;
                            data_array[0]["msg_content"] = saved.content;
                            _chat_msgs.push_back(std::make_shared<ChatMessage>(
                                saved.id,
                                data_array[0]["msg_uuid"].asString(),
                                chat_id,
                                from_uid,
                                to_uid,
                                saved.content,
                                0));
                        } catch (const messaging::PrivateSendDenied&) {
                            return_value["error"] = ErrorCodes::UidInvalid;
                            return_value["commit_error"] = "InvalidMembership";
                            return Reply(session, MSG_TEXT_CHAT_MSG_RSP, messaging::CompactJson(return_value));
                        } catch (const std::exception&) {
                            return_value["error"] = ErrorCodes::UidInvalid;
                            return_value["commit_error"] = "StorageUnavailable";
                            return Reply(session, MSG_TEXT_CHAT_MSG_RSP, messaging::CompactJson(return_value));
                        }
                    } else {
                        for (const auto& item : _cache_msgs) {
                            if (item.second.rfind("@resource:v1:", 0) == 0) {
                                return_value["error"] = ErrorCodes::UidInvalid;
                                return_value["commit_error"] = "Conflict";
                                return Reply(session, MSG_TEXT_CHAT_MSG_RSP, messaging::CompactJson(return_value));
                            }
                        }
                        result = MysqlMgr::GetInstance()->AddChatMessageList(
                            message_commit::AuthenticatedPrincipal{principal_uid},
                            from_uid,
                            to_uid,
                            chat_id,
                            _cache_msgs,
                            _chat_msgs,
                            std::chrono::steady_clock::now() + std::chrono::seconds(5),
                            group_epoch,
                            relationship);
                    }
                    if (!result.IsSuccess()) {
                        return_value["error"] = ErrorCodes::UidInvalid;
                        switch (result.error) {
                        case message_commit::Error::UNAUTHORIZED_SENDER:
                            return_value["commit_error"] = "UnauthorizedSender";
                            break;
                        case message_commit::Error::INVALID_UUID:
                            return_value["commit_error"] = "InvalidUuid";
                            break;
                        case message_commit::Error::INVALID_MEMBERSHIP:
                            return_value["commit_error"] = "InvalidMembership";
                            break;
                        case message_commit::Error::CONFLICT:
                            return_value["commit_error"] = "Conflict";
                            break;
                        case message_commit::Error::DEADLINE_EXCEEDED:
                            return_value["commit_error"] = "DeadlineExceeded";
                            break;
                        default:
                            return_value["commit_error"] = "StorageUnavailable";
                            break;
                        }
                        return Reply(session, MSG_TEXT_CHAT_MSG_RSP, messaging::CompactJson(return_value));
                    }

                    // 组织给客户端回包的数据
                    Json::Value uuid_msgId;
                    for (auto& msg : _chat_msgs) {
                        Json::Value info;
                        info["msg_uuid"] = msg->_client_msg_id;
                        info["message_id"] = msg->_message_id;
                        uuid_msgId.append(info);
                    }

                    return_value["uuid_msgId"] = uuid_msgId;
                    // Fixed-member groups use authoritative short-interval sync, including across instances.
                    if (group_message || resource_existing)
                        return Reply(session, MSG_TEXT_CHAT_MSG_RSP, messaging::CompactJson(return_value));
                    if (!resource_message) {
                        Json::Value created(Json::arrayValue);
                        std::vector<std::shared_ptr<ChatMessage>> messages;
                        for (std::size_t index = 0; index < result.items.size(); ++index) {
                            if (result.items[index].disposition != message_commit::Disposition::CREATED)
                                continue;
                            created.append(data_array[static_cast<Json::ArrayIndex>(index)]);
                            messages.push_back(_chat_msgs[index]);
                        }
                        if (messages.empty())
                            return Reply(session, MSG_TEXT_CHAT_MSG_RSP, messaging::CompactJson(return_value));
                        data_array = std::move(created);
                        _chat_msgs = std::move(messages);
                    }
                    // 提交后确认发送方；Redis 池查找接收者，远端投递交给出站 gRPC 池。
                    return ReplyAndNotify(
                        session,
                        MSG_TEXT_CHAT_MSG_RSP,
                        return_value,
                        [this,
                         from_uid,
                         to_uid,
                         chat_id,
                         data_array = std::move(data_array),
                         _chat_msgs = std::move(_chat_msgs)]() -> Completion {
                            // 查询redis查看对方的ip

                            std::string touid_ip_value = "";
                            const auto presence = _presence->Find(to_uid);
                            if (presence.status == PresenceStatus::Unavailable) {
                                SPDLOG_WARN("presence lookup unavailable");
                                return {};
                            }
                            bool b_success = presence.status == PresenceStatus::Found;
                            if (presence.presence)
                                touid_ip_value = presence.presence->server_id;
                            if (!b_success) {
                                // Routing failure cannot revoke the already committed acknowledgement.
                                return {};
                            }

                            auto& configMgr = ConfigMgr::GetInstance();
                            auto self_server_name = configMgr["SelfServer"]["Name"];

                            // 两者在同一个服务器，则直接发送
                            if (self_server_name == touid_ip_value) {
                                auto sessions = _directory;
                                auto session = FindAuthorizedRecipient(*sessions, *_presence, to_uid);
                                if (session) {
                                    // 这是往另一个客户端的通知信息
                                    Json::Value notify;
                                    notify["error"] = ErrorCodes::Success;
                                    notify["from_uid"] = from_uid;
                                    notify["to_uid"] = to_uid;
                                    notify["chat_id"] = chat_id;
                                    Json::Value notify_msgs;
                                    for (auto& msg : _chat_msgs) {
                                        Json::Value info;
                                        info["message_id"] = msg->_message_id;
                                        info["msg_uuid"] = msg->_client_msg_id;
                                        info["msg_content"] = msg->_content;
                                        notify_msgs.append(info);
                                    }

                                    notify["notify_msgs"] = notify_msgs;

                                    // 直接在这里通知
                                    std::string notify_str = messaging::BoundedNotification(notify);
                                    session->Send({MSG_NOTIFY_CHAT_MSG_REQ, notify_str});
                                }

                                return {};
                            }

                            // 两者不在同一个服务器，则通过grpc发送
                            TextChatMsgReq send_req;
                            send_req.set_fromuid(from_uid);
                            send_req.set_touid(to_uid);
                            send_req.set_chatid(chat_id);
                            int j = 0; // 同步对应的插入数据库得到的messageId
                            for (const auto& text_obj : data_array) {
                                auto msg_content = text_obj["msg_content"].asString();
                                auto uuid = text_obj["msg_uuid"].asString();
                                // 创建多个内部的变量
                                auto* text_msg = send_req.add_textmsgs();
                                text_msg->set_uuid(uuid);
                                text_msg->set_msgcontent(msg_content);
                                text_msg->set_msgid(_chat_msgs[j]->_message_id);
                                j++;
                            }
                            // 将路由结果和请求所有权交给出站池。
                            return [server = touid_ip_value, request = std::move(send_req)] {
                                ChatGrpcClient::GetInstance()->NotifyOtherReceiveTextChatMsg(server, request);
                            };
                        });
                });
        };

    // 处理客户端心跳请求
    // 核对心跳身份并维持当前会话活跃状态。
    handlers[MSG_HEART_BEAT_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            SPDLOG_TRACE("heartbeat request, msg_id={}", static_cast<int>(MSG_HEART_BEAT_REQ));
            // 解析json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err) {
                SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
                return;
            }

            auto uid = root["uid"].asInt();

            Json::Value return_value;
            return_value["error"] = ErrorCodes::Success;
            std::string return_str = return_value.toStyledString();
            session->Send(return_str, MSG_HEART_BEAT_RSP);
        };

    // 创建一个私聊请求
    // 校验双方身份并获取或创建私聊会话。
    handlers[MSG_CREATE_PRIVATE_CHAT_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            SPDLOG_DEBUG("create private chat request, msg_id={}", static_cast<int>(MSG_CREATE_PRIVATE_CHAT_REQ));
            // 解析json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err) {
                SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
                return;
            }

            auto self_id = root["self_id"].asInt();
            auto other_id = root["other_id"].asInt();

            Json::Value return_value;
            return_value["error"] = ErrorCodes::Success;
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(session, [session, self_id, other_id, return_value]() mutable -> Completion {
                int chat_id = -1;
                bool success = MysqlMgr::GetInstance()->CreatePrivateChat(self_id, other_id, chat_id);

                if (!success) {
                    return_value["error"] = ErrorCodes::UidInvalid;
                }

                return_value["self_id"] = self_id;
                return_value["other_id"] = other_id;
                return_value["chat_id"] = chat_id;
                return Reply(session, MSG_CREATE_PRIVATE_CHAT_RSP, messaging::CompactJson(return_value));
            });
        };

    // 加载一部分聊天列表请求
    // 分页读取当前用户会话列表。
    handlers[MSG_LOAD_CHAT_LIST_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            SPDLOG_DEBUG("load chat list request, msg_id={}", static_cast<int>(MSG_LOAD_CHAT_LIST_REQ));
            // 解析json数据
            Json::Reader reader;
            Json::Value root;
            auto err = reader.parse(msg_data, root);
            if (!err) {
                SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
                return;
            }

            auto uid = root["uid"].asInt();
            auto current_chat_id = root["current_chat_id"].asInt();

            Json::Value return_value;
            return_value["error"] = ErrorCodes::Success;
            return_value["uid"] = uid;
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(session, [this, session, uid, current_chat_id, return_value]() mutable -> Completion {
                std::vector<std::shared_ptr<ChatInfoBase>> chat_list;

                // 是否能够加载更多
                bool load_more = false;
                int last_load_id = 0;
                bool success =
                    MysqlMgr::GetInstance()
                        ->GetUserChatList(uid, current_chat_id, PAGE_SIZE, chat_list, load_more, last_load_id);

                if (!success) {
                    return_value["error"] = ErrorCodes::UidInvalid;
                    return Reply(session, MSG_LOAD_CHAT_LIST_RSP, messaging::CompactJson(return_value));
                }

                return_value["load_more"] = load_more;
                return_value["current_chat_id"] = last_load_id;

                for (auto& chat : chat_list) {
                    Json::Value item;

                    item["type"] = chat->_type;
                    item["chat_id"] = chat->_chat_id;

                    if (chat->_type == "private") {
                        auto child = std::dynamic_pointer_cast<PrivateChatInfo>(chat);
                        item["user1_id"] = child->_user1_id;
                        item["user2_id"] = child->_user2_id;
                    } else if (chat->_type == "group") {
                        auto child = std::dynamic_pointer_cast<GroupChatInfo>(chat);
                        item = child->_membership;
                    }

                    return_value["chat_list"].append(item);
                }
                return Reply(session, MSG_LOAD_CHAT_LIST_RSP, messaging::CompactJson(return_value));
            });
        };

    // 增量加载部分聊天数据
    // 验证参与资格并分页读取会话历史。
    handlers[MSG_LOAD_CHAT_MESSAGE_REQ] =
        [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
            SPDLOG_DEBUG("load chat message request, msg_id={}", static_cast<int>(MSG_LOAD_CHAT_MESSAGE_REQ));
            // 解析json数据
            Json::Reader reader;
            Json::Value root;
            if (!reader.parse(msg_data, root) || !root.isObject()) {
                Json::Value invalid;
                invalid["error"] = ErrorCodes::Error_Json;
                session->Send(SerializeHistoryResponse(invalid, 0), MSG_LOAD_CHAT_MESSAGE_RSP);
                return;
            }
            if (root.isMember("mode")) {
                Json::Value response;
                response["mode"] = "sync_v1";
                response["error"] = ErrorCodes::UidInvalid;
                response["chat_id"] = root["chat_id"];
                response["request_id"] = root["request_id"];
                response["after_id"] = root["after_id"];
                if (root["membership_epoch"].isString())
                    response["membership_epoch"] = root["membership_epoch"];
                if (root["mode"].isString() && root["mode"].asString() == "sync_v1" && root["uid"].isInt() &&
                    root["uid"].asInt() > 0 && root["chat_id"].isInt() && root["chat_id"].asInt() > 0 &&
                    root["after_id"].isInt64() && root["after_id"].asInt64() >= 0 && root["request_id"].isString() &&
                    root["request_id"].asString().size() <= 64 &&
                    _directory->IsCurrent(root["uid"].asInt(), session->Id())) {
                    // 在数据库线程执行本次请求的同步存取。
                    QueueDatabase(session, [session, root, response]() mutable -> Completion {
                        response["error"] = ErrorCodes::Success;
                        if (!MysqlMgr::GetInstance()->SyncChatMessages(
                                root["uid"].asInt(),
                                root["chat_id"].asInt(),
                                root["after_id"].asInt64(),
                                response)) {
                            response["error"] = ErrorCodes::UidInvalid;
                            response.removeMember("msgs");
                        }
                        return Reply(session, MSG_LOAD_CHAT_MESSAGE_RSP, messaging::CompactJson(response));
                    });
                    return;
                }
                session->Send(messaging::CompactJson(response), MSG_LOAD_CHAT_MESSAGE_RSP);
                return;
            }
            Json::Value return_value;
            return_value["error"] = ErrorCodes::Error_Json;
            if (!root["chat_id"].isInt() || root["chat_id"].asInt() <= 0 || !root["current_msg_id"].isInt() ||
                root["current_msg_id"].asInt() < 0) {
                session->Send(SerializeHistoryResponse(return_value, 0), MSG_LOAD_CHAT_MESSAGE_RSP);
                return;
            }
            const int chat_id = root["chat_id"].asInt();
            const int current_msg_id = root["current_msg_id"].asInt();
            return_value["error"] = ErrorCodes::Success;
            return_value["chat_id"] = chat_id;
            // 在数据库线程执行本次请求的同步存取。
            QueueDatabase(
                session,
                [this, session, uid = session->AuthenticatedUid(), chat_id, current_msg_id, return_value]() mutable
                    -> Completion {
                    std::vector<std::shared_ptr<ChatMessage>> chat_msgs;

                    // 是否能够加载更多
                    bool load_more = false;
                    int last_msg_id = 0;
                    bool success = MysqlMgr::GetInstance()->GetChatMessageList(
                        uid,
                        chat_id,
                        current_msg_id,
                        PAGE_SIZE,
                        chat_msgs,
                        load_more,
                        last_msg_id);

                    if (!success) {
                        return_value["error"] = ErrorCodes::UidInvalid;
                        return Reply(
                            session,
                            MSG_LOAD_CHAT_MESSAGE_RSP,
                            SerializeHistoryResponse(return_value, current_msg_id));
                    }

                    return_value["load_more"] = load_more;
                    return_value["current_msg_id"] = last_msg_id;

                    for (auto& chat : chat_msgs) {
                        Json::Value item;

                        item["message_id"] = chat->_message_id;
                        item["msg_uuid"] = chat->_client_msg_id;
                        item["send_id"] = chat->_send_id;
                        item["recv_id"] = chat->_recv_id;
                        item["content"] = chat->_content;
                        item["status"] = chat->_status;
                        item["created_at"] = chat->_created_at;

                        return_value["msgs"].append(item);
                    }
                    return Reply(
                        session,
                        MSG_LOAD_CHAT_MESSAGE_RSP,
                        SerializeHistoryResponse(return_value, current_msg_id));
                });
        };
    return handlers;
}

/** @brief 只投影可公开用户字段；登录与搜索共享数据库权威资料。 */
static void PublicUser(const std::shared_ptr<UserInfo>& user, Json::Value& value) {
    value["error"] = user ? ErrorCodes::Success : ErrorCodes::UidInvalid;
    if (!user)
        return;
    value["uid"] = user->_uid;
    value["name"] = user->_name;
    value["description"] = user->_description;
    value["icon"] = user->_icon;
    value["sex"] = user->_sex;
}
void LogicSystem::GetUserByUid(std::string uid, Json::Value& value) {
    try {
        PublicUser(MysqlMgr::GetInstance()->GetUserByUid(std::stoi(uid)), value);
    } catch (const std::exception&) {
        value["error"] = ErrorCodes::UidInvalid;
    }
}
void LogicSystem::GetUserByName(std::string name, Json::Value& value) {
    PublicUser(MysqlMgr::GetInstance()->GetUserByName(name), value);
}

bool LogicSystem::IsOnlyDigit(std::string& uid_name) {
    for (char ch : uid_name) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }
    return true;
}
