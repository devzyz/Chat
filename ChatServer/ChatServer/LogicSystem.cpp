#include "../../common/message/MessagePersistence.h"
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
#include "StatusGrpcClient.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "UserSessionDirectory.h"
#include "ChatGrpcClient.h"
#include "CServer.h"
#include "LogMgr.h"

LogicSystem::LogicSystem(std::shared_ptr<UserSessionDirectory> directory,
    std::shared_ptr<UserPresenceStore> presence)
	: LogicDispatcher([this](const LogicMessage& message) { return Dispatch(message); }),
	  _directory(std::move(directory)), _presence(std::move(presence)) {
	RegisterCallBacks();
}

LogicSystem::~LogicSystem() {
	Stop();
}

bool LogicSystem::Dispatch(const LogicMessage& message) {
	SPDLOG_DEBUG("logic recv msg, msg_id={}", message.id);
	const auto callback = _fun_callbacks.find(message.id);
	if (callback == _fun_callbacks.end()) {
		return false;
	}
    if (message.id != MSG_CHAT_LOGIN_REQ) {
        const int uid = message.session->AuthenticatedUid();
        if (uid <= 0 || !_directory->IsCurrent(uid, message.session->Id())) return true;
        Json::Value request;
        Json::Reader reader;
        if (!reader.parse(message.body, request) || !request.isObject()) return true;
        for (const auto* field : {"uid", "fromuid", "authuid", "from_uid", "self_id"}) {
            if (request.isMember(field) && (!request[field].isInt() || request[field].asInt() != uid)) {
                message.session->Close(SessionCloseReason::ProtocolError);
                return true;
            }
        }
    }
	callback->second(message.session, message.id, message.body);
	return true;
}

/**
 * @brief 
 * 回调函数注册位置
 */
void LogicSystem::RegisterCallBacks() {
	// 处理用户登录请求
	_fun_callbacks[MSG_CHAT_LOGIN_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
		// 解析msg_data对应的json数据
		Json::Reader reader;
		Json::Value root;
		auto err = reader.parse(msg_data, root);
		if (!err) {
			SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
			return;
		}

		int uid = root["uid"].asInt();
		std::string token = root["token"].asString();
		SPDLOG_INFO("user login request, uid={}", uid);

		// 主要目的是查询状态，看是否满足token条件
		auto rsp = StatusGrpcClient::GetInstance()->Login(uid, token);

		Json::Value return_value;
		// 自动返回函数，当函数执行到右括号后，局部变量会被析构，此时defer被析构，其析构时，回调用lambda函数
		bool binding_started = false;
		Defer defer1([this, &return_value, &binding_started, session]() {
            if (binding_started) return;
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_CHAT_LOGIN_RSP);
			});

		// 如果状态服务查询出错误，则返回
		return_value["error"] = rsp.error();
		if (rsp.error() != ErrorCodes::Success) {
			return;
		}
		// 这里查询的是用户的基本信息，先查redis，redis查不到，则会查mysql
		// 这里如果更在变更信息，同时又去读取。但并不会产生很大的错误，最多只会出现读取的是旧的数据
		// 因此不用添加分布式锁，这样可以提高效率
		// 查询用户的基本信息，如果redis中没有，则去mysql中查询
		std::string baseinfo_key = USER_BASE_INFO + std::to_string(uid);
		auto user_info = std::make_shared<UserInfo>();
		bool success = GetUserBaseInfo(baseinfo_key, uid, user_info);
		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
		}

		return_value["error"] = ErrorCodes::Success;
		return_value["uid"] = uid;
		return_value["name"] = user_info->_name;
		return_value["description"] = user_info->_description;
		return_value["icon"] = user_info->_icon;
		return_value["email"] = user_info->_email;
		return_value["sex"] = user_info->_sex;
		return_value["token"] = token;

		// 同样的，这里也不需要添加分布式锁
		// 因为刚好有两个相同uid=1的客户端，然后另一个客户端uid=2要添加uid=1为好友，但是我们一定会写入到数据库中
		// 所以即使我查到的是旧的serverip信息，也能够接受，当下一次上线的时候，会重新收到好友请求信息
		// 获取好友申请列表
		std::vector<std::shared_ptr<ApplyInfo>> apply_list;
		auto b_apply = GetApplyFriendList(uid, apply_list);
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
		auto b_user = GetFriendList(uid, friend_list);
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
			bool success = GetUserChatList(uid, 0, PAGE_SIZE, chat_list, load_more, last_load_id);

			if (!success) {
				return_value["error"] = ErrorCodes::UidInvalid;
				return;
			}

			return_value["load_more"] = load_more;
			return_value["current_chat_id"] = last_load_id;

			for (auto& chat : chat_list) {
				Json::Value item;

				item["type"] = chat->_type;
				item["chat_id"] = chat->_chat_id;

				if (chat->_type == "private") {
					auto child = std::dynamic_pointer_cast<PrivateChatInfo> (chat);
					item["user1_id"] = child->_user1_id;
					item["user2_id"] = child->_user2_id;
				}
				else if (chat->_type == "group") {
					auto child = std::dynamic_pointer_cast<GroupChatInfo> (chat);
					item["group_name"] = child->_group_name;
				}

				return_value["chat_list"].append(item);
			}
		}

		// 添加分布式锁
        binding_started = true;
        session->BindAuthenticatedUser(uid, [session, response = std::move(return_value)](SessionBindResult result) mutable {
            if (result != SessionBindResult::Bound) response["error"] = ErrorCodes::RPCFailed;
            session->Send(response.toStyledString(), MSG_CHAT_LOGIN_RSP);
        });

	};

	// 处理搜索用户的请求
	_fun_callbacks[MSG_SEARCH_USER_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) -> void{
		// 解析msg_data对应的json数据
		Json::Reader reader;
		Json::Value root;
		auto err = reader.parse(msg_data, root);
		if (!err) {
			SPDLOG_WARN("json parse failure, msg_id={}", msg_id);
			return;
		}

		auto uid_name = root["uid_name"].asString();
		SPDLOG_DEBUG("user search request, uid_name={}", uid_name);

		Json::Value return_value;
		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_SEARCH_USER_RSP);
			});

		// 用户可通过uid/name两种类型搜索，当只有数字时，判定通过uid搜索，否则判定为name搜索
		bool b_digit = IsOnlyDigit(uid_name);

		if (b_digit) {
			GetUserByUid(uid_name, return_value);
		}
		else {
			GetUserByName(uid_name, return_value);
		}
	};
	
	// 处理申请好友的请求
	_fun_callbacks[MSG_ADD_FRIEND_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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
		auto touid = root["touid"].asInt(); // 申请添加touid为好友
		auto description = root["description"].asString(); // 申请信息
		auto backname = root["backname"].asString(); // 申请人给touid的备注名
		
		SPDLOG_DEBUG("add friend request, fromuid={}, touid={}, description_size={}, backname_size={}", fromuid, touid, description.size(), backname.size());

		Json::Value return_value;
		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_ADD_FRIEND_RSP);
			});

		return_value["error"] = ErrorCodes::Success;
		// 先更新数据库

        // Identity belongs to the authenticated connection, not the request body.
        if (session->AuthenticatedUid() <= 0 || fromuid != session->AuthenticatedUid() ||
            touid <= 0 || fromuid == touid) {
            return_value["error"] = ErrorCodes::UidInvalid;
            return;
        }
        auto recipient = std::make_shared<UserInfo>();
        if (!GetUserBaseInfo(USER_BASE_INFO + std::to_string(touid), touid, recipient)) {
            return_value["error"] = ErrorCodes::UidInvalid;
            return;
        }
		bool success = MysqlMgr::GetInstance()->AddFriendApply(fromuid, touid, description, backname);
		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
		}

		// 接下来查找touid连接到哪一个服务器上，如果在本服务器，则直接转发，否则通过grpc转发
		// 如果查不到，代表touid已离线，则直接返回
		auto to_uid_str = std::to_string(touid);

		std::string to_ip_value = "";
		const auto presence = _presence->Find(touid);
        if (presence.status == PresenceStatus::Unavailable) { SPDLOG_WARN("presence lookup unavailable"); return; }
        bool b_to_ip = presence.status == PresenceStatus::Found;
        if (presence.presence) to_ip_value = presence.presence->server_id;
		if (!b_to_ip) {
			return;
		}

		// 接下来判断是否跟自己在同一份服务器
		auto& configMgr = ConfigMgr::GetInstance();
		auto self_name = configMgr["SelfServer"]["Name"];

		// 查询到在同一服务器
		if (to_ip_value == self_name) {
			auto sessions = _directory;
			auto touid_session = sessions->FindCurrent(touid);
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
			return;
		}

		// 查询一下自己的信息
		std::string baseinfo_key = USER_BASE_INFO + std::to_string(fromuid);
		std::shared_ptr<UserInfo> user_info = std::make_shared<UserInfo> ();
		bool b_info = GetUserBaseInfo(baseinfo_key, fromuid, user_info);

		// 通过grpc通知另一个服务器添加好友
		AddFriendReq req;
		req.set_applyuid(fromuid);
		req.set_touid(touid);
		req.set_description(description);
		req.set_backname(backname);
		if (b_info) {
			req.set_applyname(user_info->_name);
			req.set_applydescription(user_info->_description);
			req.set_applyicon(user_info->_icon);
			req.set_applysex(user_info->_sex);
		}

		// 发送grpc请求
		ChatGrpcClient::GetInstance()->NotifyOtherAddFriend(to_ip_value, req);
	};

	// 处理认证好友的请求
	_fun_callbacks[MSG_AUTH_FRIEND_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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
		auto authuid = root["authuid"].asInt(); // 被申请人uid
		auto applyinfo = root["applyinfo"]; // 申请人信息
		auto authinfo = root["authinfo"]; // 认证人信息

		// 返回值
		Json::Value return_value;
		return_value["error"] = ErrorCodes::Success;
		return_value["applyuid"] = applyuid;
		return_value["authuid"] = authuid;
		return_value["applyinfo"] = applyinfo;
		return_value["authinfo"] = authinfo;
	
		// 发送回包
		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_AUTH_FRIEND_RSP);
			});

		std::vector<std::shared_ptr<ChatMessage>> _chat_msgs;

        if (session->AuthenticatedUid() <= 0 || authuid != session->AuthenticatedUid() ||
            applyuid <= 0 || applyuid == authuid || !applyinfo.isObject() || !authinfo.isObject() ||
            !applyinfo["applyuid"].isInt() || applyinfo["applyuid"].asInt() != applyuid ||
            !applyinfo["touid"].isInt() || applyinfo["touid"].asInt() != authuid ||
            !authinfo["authuid"].isInt() || authinfo["authuid"].asInt() != authuid ||
            !authinfo["touid"].isInt() || authinfo["touid"].asInt() != applyuid) {
            return_value["error"] = ErrorCodes::UidInvalid;
            return;
        }
		int chat_id = 0;
		SPDLOG_DEBUG("auth friend description received, applyuid={}, authuid={}, description_size={}", applyuid, authuid, authinfo["description"].asString().size());
		// 更新数据库
		bool success = MysqlMgr::GetInstance()->AuthFriendApply(applyuid, authuid, 
			applyinfo["backname"].asString(), authinfo["backname"].asString(), 
			applyinfo["description"].asString(), authinfo["description"].asString(), 
			_chat_msgs, chat_id);

		return_value["chatid"] = chat_id;

		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
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

		// 通知对方认证成功
		// 先查询redis，查看对方的server_ip
		auto applyuid_str = std::to_string(applyuid);

		std::string applyuid_ip_value = "";
		const auto presence = _presence->Find(applyuid);
        if (presence.status == PresenceStatus::Unavailable) { SPDLOG_WARN("presence lookup unavailable"); return; }
        bool isSuccess = presence.status == PresenceStatus::Found;
        if (presence.presence) applyuid_ip_value = presence.presence->server_id;
		// 查询不到，说明对方已离线，则不对界面进行更新。因为已经对数据库更新了，因此下次登录是正确结果
		if (!isSuccess) {
			return;
		}

		// 拿出自己的server_ip查看是否相同
		auto& configMgr = ConfigMgr::GetInstance();
		auto self_server_name = configMgr["SelfServer"]["Name"];

		// 两个人在同一个服务器上，则直接找到对方的session，并发送请求
		if (self_server_name == applyuid_ip_value) {
			auto sessions = _directory;
			auto session = sessions->FindCurrent(applyuid);
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
				std::string notify_str = notify.toStyledString();
				session->Send({MSG_NOTIFY_AUTH_FRIEND_REQ, notify_str});
			}

			return;
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

		ChatGrpcClient::GetInstance()->NotifyOtherAuthFriend(applyuid_ip_value, auth_req);
	};

	// 处理用户发送的文本请求
	_fun_callbacks[MSG_TEXT_CHAT_MSG_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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

		Json::Value return_value;

		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_TEXT_CHAT_MSG_RSP);
			});

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
            return;
        }
        if (!data_array.isArray() || data_array.empty() || to_uid <= 0 || chat_id <= 0) {
            return_value["error"] = ErrorCodes::Error_Json;
            return_value["commit_error"] = "InvalidMembership";
            return;
        }
        std::vector<std::pair<std::string, std::string>> _cache_msgs;
		std::vector<std::shared_ptr<ChatMessage>> _chat_msgs;
		for (auto& msg : data_array) {
            if (!msg.isObject() || !msg["msg_content"].isString() || !msg["msg_uuid"].isString()) {
                return_value["error"] = ErrorCodes::Error_Json;
                return_value["commit_error"] = "InvalidUuid";
                return;
            }
			auto msg_content = msg["msg_content"].asString();
			auto msg_uuid = msg["msg_uuid"].asString();
			_cache_msgs.push_back({ msg_uuid, msg_content });
		}

        message_commit::Result result;
        if (resource_message) {
            try {
                auto sessions = _directory;
                if (!sessions->IsCurrent(from_uid, session->Id()) || data_array.size() != 1
                    || !root["resource_id"].isString()
                    || !message_commit::IsCanonicalUuid(data_array[0]["msg_uuid"].asString()))
                    throw std::runtime_error("invalid resource sender");
                auto& config = ConfigMgr::GetInstance();
                static resource::ResourceCatalog catalog(config["Mysql"]["Host"] + ":" + config["Mysql"]["Port"],
                    config["Mysql"]["User"], config["Mysql"]["Password"], config["Mysql"]["Schema"]);
                auto saved = catalog.CommitMessage(from_uid, to_uid, chat_id,
                    data_array[0]["msg_uuid"].asString(), root["resource_id"].asString());
                data_array[0]["msg_content"] = saved.content;
                _chat_msgs.push_back(std::make_shared<ChatMessage>(saved.id,
                    data_array[0]["msg_uuid"].asString(), chat_id, from_uid, to_uid, saved.content, 0));
            } catch (const std::exception&) {
                return_value["error"] = ErrorCodes::UidInvalid;
                return_value["commit_error"] = "StorageUnavailable";
                return;
            }
        } else {
            for (const auto& item : _cache_msgs) {
                if (item.second.rfind("@resource:v1:", 0) == 0) {
                    return_value["error"] = ErrorCodes::UidInvalid;
                    return_value["commit_error"] = "Conflict";
                    return;
                }
            }
            result = MysqlMgr::GetInstance()->AddChatMessageList(
                message_commit::AuthenticatedPrincipal{principal_uid}, from_uid, to_uid, chat_id,
                _cache_msgs, _chat_msgs, std::chrono::steady_clock::now() + std::chrono::seconds(5));
        }
        if (!result.IsSuccess()) {
			return_value["error"] = ErrorCodes::UidInvalid;
            switch (result.error) {
            case message_commit::Error::UNAUTHORIZED_SENDER: return_value["commit_error"] = "UnauthorizedSender"; break;
            case message_commit::Error::INVALID_UUID: return_value["commit_error"] = "InvalidUuid"; break;
            case message_commit::Error::INVALID_MEMBERSHIP: return_value["commit_error"] = "InvalidMembership"; break;
            case message_commit::Error::CONFLICT: return_value["commit_error"] = "Conflict"; break;
            case message_commit::Error::DEADLINE_EXCEEDED: return_value["commit_error"] = "DeadlineExceeded"; break;
            default: return_value["commit_error"] = "StorageUnavailable"; break;
            }
			return;
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

		// 查询redis查看对方的ip

		std::string touid_ip_value = "";
		const auto presence = _presence->Find(to_uid);
        if (presence.status == PresenceStatus::Unavailable) { SPDLOG_WARN("presence lookup unavailable"); return; }
        bool b_success = presence.status == PresenceStatus::Found;
        if (presence.presence) touid_ip_value = presence.presence->server_id;
		if (!b_success) {
            // Routing failure cannot revoke the already committed acknowledgement.
			return;
		}

		auto& configMgr = ConfigMgr::GetInstance();
		auto self_server_name = configMgr["SelfServer"]["Name"];

		// 两者在同一个服务器，则直接发送
		if (self_server_name == touid_ip_value) {
			auto sessions = _directory;
			auto session = sessions->FindCurrent(to_uid);
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
				std::string notify_str = notify.toStyledString();
				session->Send({MSG_NOTIFY_CHAT_MSG_REQ, notify_str});
			}
			
			return;
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
			auto * text_msg = send_req.add_textmsgs();
			text_msg->set_uuid(uuid);
			text_msg->set_msgcontent(msg_content);
			text_msg->set_msgid(_chat_msgs[j]->_message_id);
			j++;
		}
		ChatGrpcClient::GetInstance()->NotifyOtherReceiveTextChatMsg(touid_ip_value, send_req);
	};

	// 处理客户端心跳请求
	_fun_callbacks[MSG_HEART_BEAT_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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
	_fun_callbacks[MSG_CREATE_PRIVATE_CHAT_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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

		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_CREATE_PRIVATE_CHAT_RSP);
			});

		int chat_id = -1;
		bool success = MysqlMgr::GetInstance()->CreatePrivateChat(self_id, other_id, chat_id);

		
		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
		}
		
		return_value["self_id"] = self_id;
		return_value["other_id"] = other_id;
		return_value["chat_id"] = chat_id;
	};

	// 加载一部分聊天列表请求
	_fun_callbacks[MSG_LOAD_CHAT_LIST_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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

		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_LOAD_CHAT_LIST_RSP);
			});

		std::vector<std::shared_ptr<ChatInfoBase>> chat_list;

		// 是否能够加载更多
		bool load_more = false;
		int last_load_id = 0;
		bool success = GetUserChatList(uid, current_chat_id, PAGE_SIZE, chat_list, load_more, last_load_id);

		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
		}

		return_value["load_more"] = load_more;
		return_value["current_chat_id"] = last_load_id;

		for (auto& chat : chat_list) {
			Json::Value item;

			item["type"] = chat->_type;
			item["chat_id"] = chat->_chat_id;

			if (chat->_type == "private") {
				auto child = std::dynamic_pointer_cast<PrivateChatInfo> (chat);
				item["user1_id"] = child->_user1_id;
				item["user2_id"] = child->_user2_id;
			}
			else if (chat->_type == "group") {
				auto child = std::dynamic_pointer_cast<GroupChatInfo> (chat);
				item["group_name"] = child->_group_name;
			}

			return_value["chat_list"].append(item);
		}


	};

	// 增量加载部分聊天数据
	_fun_callbacks[MSG_LOAD_CHAT_MESSAGE_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
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
            if (root["mode"].isString() && root["mode"].asString() == "sync_v1"
                && root["uid"].isInt() && root["uid"].asInt() > 0
                && root["chat_id"].isInt() && root["chat_id"].asInt() > 0
                && root["after_id"].isInt64() && root["after_id"].asInt64() >= 0
                && root["request_id"].isString() && root["request_id"].asString().size() <= 64
                && _directory->IsCurrent(root["uid"].asInt(), session->Id())) {
                response["error"] = ErrorCodes::Success;
                if (!MysqlMgr::GetInstance()->SyncChatMessages(root["uid"].asInt(), root["chat_id"].asInt(),
                    root["after_id"].asInt64(), response)) {
                    response["error"] = ErrorCodes::UidInvalid;
                    response.removeMember("msgs");
                }
            }
            session->Send(messaging::CompactJson(response), MSG_LOAD_CHAT_MESSAGE_RSP);
            return;
        }
        Json::Value return_value;
        return_value["error"] = ErrorCodes::Error_Json;
        int request_cursor = 0;
        Defer defer([&return_value, &request_cursor, session]() {
            session->Send(SerializeHistoryResponse(return_value, request_cursor), MSG_LOAD_CHAT_MESSAGE_RSP);
        });
        if (!root["chat_id"].isInt() || root["chat_id"].asInt() <= 0 ||
            !root["current_msg_id"].isInt() || root["current_msg_id"].asInt() < 0) return;
        const int chat_id = root["chat_id"].asInt();
        const int current_msg_id = root["current_msg_id"].asInt();
        request_cursor = current_msg_id;
        return_value["error"] = ErrorCodes::Success;
        return_value["chat_id"] = chat_id;

		std::vector<std::shared_ptr<ChatMessage>> chat_msgs;

		// 是否能够加载更多
		bool load_more = false;
		int last_msg_id = 0;
		bool success = GetChatMessageList(session->AuthenticatedUid(), chat_id, current_msg_id, PAGE_SIZE, chat_msgs, load_more, last_msg_id);

		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
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
	};
}

/**
 * @brief 
 * @param uid 
 * @param value 
 * 先从redis中查询用户信息，如果未查询到，则去mysql中查询，并进行更新
 */
void LogicSystem::GetUserByUid(std::string uid, Json::Value& value) {
	std::string key = USER_BASE_INFO + uid;
	// 从redis中查询用户信息
	std::string info_str = "";
	bool b_info = RedisMgr::GetInstance()->Get(key, info_str);
	// 查询到用户信息
	if (b_info) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);

		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto password = root["password"].asString();
		auto email = root["email"].asString();
		auto description = root["description"].asString();
		auto icon = root["icon"].asString();
		auto sex = root["sex"].asInt();

		SPDLOG_DEBUG("search user cache hit by uid, uid={}", uid);
		
		value["error"] = ErrorCodes::Success;
		value["uid"] = uid;
		value["name"] = name;
		value["password"] = password;
		value["email"] = email;
		value["description"] = description;
		value["icon"] = icon;
		value["sex"] = sex;
		return;
	}
	// redis中不存在，则查询数据库
	auto uid_int = std::stoi(uid);
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUesr(uid_int);
	if (user_info == nullptr) {
		value["error"] = ErrorCodes::UidInvalid;
		return;
	}

	// 更新redis
	Json::Value redis_value;
	redis_value["uid"] = user_info->_uid;
	redis_value["name"] = user_info->_name;
	redis_value["password"] = user_info->_password;
	redis_value["email"] = user_info->_email;
	redis_value["description"] = user_info->_description;
	redis_value["icon"] = user_info->_icon;
	redis_value["sex"] = user_info->_sex;
	// 更新到redis中
	RedisMgr::GetInstance()->Set(key, redis_value.toStyledString());

	value["error"] = ErrorCodes::Success;
	value["uid"] = user_info->_uid;
	value["name"] = user_info->_name;
	value["password"] = user_info->_password;
	value["email"] = user_info->_email;
	value["description"] = user_info->_description;
	value["icon"] = user_info->_icon;
	value["sex"] = user_info->_sex;

	return;
}

/**
 * @brief 
 * @param name 
 * @param value 
 * 根据name查询用户信息
 */
void LogicSystem::GetUserByName(std::string name, Json::Value& value) {
	std::string key = USER_NAME_INFO + name;
	// 从redis中查询用户信息
	std::string info_str = "";
	bool b_info = RedisMgr::GetInstance()->Get(key, info_str);
	// 查询到用户信息
	if (b_info) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);

		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto password = root["password"].asString();
		auto email = root["email"].asString();
		auto description = root["description"].asString();
		auto icon = root["icon"].asString();
		auto sex = root["sex"].asInt();

		SPDLOG_DEBUG("search user cache hit by name, uid={}", uid);

		value["error"] = ErrorCodes::Success;
		value["uid"] = uid;
		value["name"] = name;
		value["password"] = password;
		value["email"] = email;
		value["description"] = description;
		value["icon"] = icon;
		value["sex"] = sex;
		return;
	}
	// redis中不存在，则查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUserByName(name);
	if (user_info == nullptr) {
		value["error"] = ErrorCodes::UidInvalid;
		return;
	}

	// 更新redis
	Json::Value redis_value;
	redis_value["uid"] = user_info->_uid;
	redis_value["name"] = user_info->_name;
	redis_value["password"] = user_info->_password;
	redis_value["email"] = user_info->_email;
	redis_value["description"] = user_info->_description;
	redis_value["icon"] = user_info->_icon;
	redis_value["sex"] = user_info->_sex;
	// 更新到redis中
	RedisMgr::GetInstance()->Set(key, redis_value.toStyledString());

	value["error"] = ErrorCodes::Success;
	value["uid"] = user_info->_uid;
	value["name"] = user_info->_name;
	value["password"] = user_info->_password;
	value["email"] = user_info->_email;
	value["description"] = user_info->_description;
	value["icon"] = user_info->_icon;
	value["sex"] = user_info->_sex;

	return;
}

/**
 * @brief 
 * @param uid_name 
 * @return 
 * 判断字符串uid_name是不是只包含数字
 */
bool LogicSystem::IsOnlyDigit(std::string& uid_name) {
	for (char ch : uid_name) {
		if (!std::isdigit(ch)) {
			return false;
		}
	}
	return true;
}

/**
 * @brief 
 * @param key 
 * @param uid 
 * @param userinfo 
 * @return 
 * 从redis中查询某个用户的信息，如果redis不存在，则去mysql中查询，并更新到redis中
 */
bool LogicSystem::GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
	// 先在redis中查询
	std::string info_str = "";
	bool success = RedisMgr::GetInstance()->Get(baseinfo_key, info_str);

	// redis中查询成功
	if (success) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);

		userinfo->_uid = root["uid"].asInt();
		userinfo->_name = root["name"].asString();
		userinfo->_password = root["password"].asString();
		userinfo->_email = root["email"].asString();
		userinfo->_description = root["description"].asString();
		userinfo->_icon = root["icon"].asString();
		userinfo->_sex = root["sex"].asInt();
	}
	else {
		// redis中没有，则去mysql中查询
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUesr(uid);
		if (user_info == nullptr) {
			return false;
		}
		userinfo = user_info;
		// 将结果写入redis
		Json::Value redis_root;
		redis_root["uid"] = userinfo->_uid;
		redis_root["name"] = userinfo->_name;
		redis_root["password"] = userinfo->_password;
		redis_root["description"] = userinfo->_description;
		redis_root["icon"] = userinfo->_icon;
		redis_root["email"] = userinfo->_email;
		redis_root["sex"] = userinfo->_sex;

		// json数据序列化，并写入redis
		RedisMgr::GetInstance()->Set(baseinfo_key, redis_root.toStyledString());
	}

	return true;
}

/**
 * @brief 
 * @param uid 
 * @param applylist 
 * 从数据库中，获取到申请添加uid为好友的请求列表
 */
bool LogicSystem::GetApplyFriendList(int uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist) {
	return MysqlMgr::GetInstance()->GetApplyFriendList(uid, applylist, 0, 10);
}

/**
 * @brief 
 * @param uid 
 * @param friend_list 
 * @return 
 * 获取用户好友列表，从Mysql中查询
 */
bool LogicSystem::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list) {
	return MysqlMgr::GetInstance()->GetFriendList(uid, friend_list);
}

/**
 * @brief
 * @param uid
 * @param friend_list
 * @return
 * 获取一页的会话列表
 */
bool LogicSystem::GetUserChatList(int uid, int current_chat_id, int page_size,
	std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_chat_id) {
	return MysqlMgr::GetInstance()->GetUserChatList(uid, current_chat_id, page_size, chat_list, load_more, last_chat_id);
}

/**
 * @brief 
 * @param chat_id 
 * @param current_load_id 
 * @param page_size 
 * @param chat_list 
 * @param load_more 
 * @param last_load_id 
 * @return 
 * 增量加载部分聊天数据
 */
bool LogicSystem::GetChatMessageList(int principal_uid, int chat_id, int current_msg_id, int page_size,
	std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id) {
	return MysqlMgr::GetInstance()->GetChatMessageList(principal_uid, chat_id, current_msg_id, page_size, chat_list, load_more, last_msg_id);
}
