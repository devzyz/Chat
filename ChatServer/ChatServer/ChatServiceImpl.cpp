#include "ChatServiceImpl.h"
#include "UserMgr.h"
#include "Const.h"
#include <json/value.h>
#include "CSession.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include <json/reader.h>
#include "CServer.h"
#include "LogMgr.h"

ChatServiceImpl::ChatServiceImpl() : _p_server(nullptr)
{

}

void ChatServiceImpl::SetServer(std::shared_ptr<chat_transport::CServer> pserver) {
	_p_server = pserver;
}

// 别的服务器通知本服务器进行好友申请信息
Status ChatServiceImpl::NotifyOtherAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response) {
	// 查看是否在本服务器，因为有可能已经离线了
	auto touid = request->touid();
	auto sessions = UserMgr::GetInstance()->Sessions();
	auto session = sessions->FindCurrent(touid);

	// 设置返回值
	response->set_error(ErrorCodes::Success);
	response->set_applyuid(request->applyuid());
	response->set_touid(request->touid());
	response->set_applyname(request->applyname());
	response->set_applydescription(request->applydescription());
	response->set_applysex(request->applysex());
	response->set_applyicon(request->applyicon());
	response->set_description(request->description());
	response->set_backname(request->backname());

	// 用户会话连接已经断开，用户已下线
	if (!session) {
		return Status::OK;
	}

	// 在内存中，则直接发送通知
	Json::Value return_value;
	return_value["error"] = ErrorCodes::Success;
	// 这里grpc传的是applyuid,对应的其实就是fromuid
	return_value["fromuid"] = request->applyuid();
	return_value["applyname"] = request->applyname();
	return_value["applydescription"] = request->applydescription();
	return_value["applyicon"] = request->applyicon();
	return_value["applysex"] = request->applysex();
	return_value["touid"] = request->touid();
	return_value["description"] = request->description();
	return_value["backname"] = request->backname();

	sessions->Send(session, {MSG_NOTIFY_ADD_FRIEND_REQ, return_value.toStyledString()});

	return Status::OK;
}

// 别的服务器通知本服务其进行认证信息
Status ChatServiceImpl::NotifyOtherAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response) {
	SPDLOG_DEBUG("notify auth friend request, applyuid={}, authuid={}, chatid={}", request->applyuid(), request->authuid(), request->chatid());
	// 查看是否在本服务器，因为有可能已经离线了
	auto applyuid = request->applyuid();
	auto authuid = request->authuid();
	auto chatid = request->chatid();
	// 由认证人发送到申请人
	auto sessions = UserMgr::GetInstance()->Sessions();
	auto session = sessions->FindCurrent(applyuid);

	// 设置返回值
	response->set_error(ErrorCodes::Success);
	
	// 对方服务器也没有，则用户已下线
	if (!session) {
		return Status::OK;
	}

	SPDLOG_DEBUG("auth friend notify session found, applyuid={}, authuid={}", applyuid, authuid);

	// 当前连接还在，则进行通知
	Json::Value notify;
	notify["error"] = ErrorCodes::Success;
	notify["applyuid"] = applyuid;
	notify["authuid"] = authuid;
	notify["chatid"] = chatid;
	// 组装数据 申请人信息
	{
		auto applyinfo = request->applyinfo();
		Json::Value info;
		info["applyuid"] = applyinfo.applyuid();
		info["applyname"] = applyinfo.applyname();
		info["applydescription"] = applyinfo.applydescription();
		info["applyicon"] = applyinfo.applyicon();
		info["applysex"] = applyinfo.applysex();
		info["touid"] = applyinfo.touid();
		info["status"] = applyinfo.status();
		info["description"] = applyinfo.description();
		info["backname"] = applyinfo.backname();
		notify["applyinfo"] = info;
	}
	// 组装数据 被申请人信息
	{
		auto authinfo = request->authinfo();
		Json::Value info;
		info["authuid"] = authinfo.authuid();
		info["authname"] = authinfo.authname();
		info["authdescription"] = authinfo.authdescription();
		info["authicon"] = authinfo.authicon();
		info["authsex"] = authinfo.authsex();
		info["touid"] = authinfo.touid();
		info["status"] = authinfo.status();
		info["description"] = authinfo.description();
		info["backname"] = authinfo.backname();
		notify["authinfo"] = info;
	}
	// 组装数据 发送的打招呼聊天数据
	{
		for (auto msg : request->chatmessage()) {
			Json::Value msg_info;
			msg_info["message_id"] = msg.messageid();
            msg_info["msg_uuid"] = msg.client_msg_uuid();
			msg_info["chat_id"] = msg.chatid();
			msg_info["send_id"] = msg.sendid();
			msg_info["recv_id"] = msg.recvid();
			msg_info["content"] = msg.content();
			msg_info["status"] = msg.status();
			notify["chat_msgs"].append(msg_info);
		}
	}

	SPDLOG_DEBUG("auth friend notify prepared, applyuid={}, authuid={}, chatid={}", applyuid, authuid, chatid);

	std::string notify_str = notify.toStyledString();
	sessions->Send(session, {MSG_NOTIFY_AUTH_FRIEND_REQ, notify_str});
	return Status::OK;
}

// 别的服务器通知接收数据
Status ChatServiceImpl::NotifyOtherReceiveTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response) {
	SPDLOG_DEBUG("notify text chat message request, from_uid={}, to_uid={}, chat_id={}, msg_count={}", request->fromuid(), request->touid(), request->chatid(), request->textmsgs_size());

	// 查看是否在本服务器，因为有可能已经离线了
	auto touid = request->touid();
	auto sessions = UserMgr::GetInstance()->Sessions();
	auto session = sessions->FindCurrent(touid);

	// 设置返回值
	response->set_error(ErrorCodes::Success);

	// 对方服务器也没有，则用户已下线
	if (!session) {
		SPDLOG_DEBUG("notify text chat skipped, target session not found, to_uid={}", touid);
		return Status::OK;
	}

	// 当前连接还在，则进行通知
	Json::Value notify;
	notify["error"] = ErrorCodes::Success;
	notify["from_uid"] = request->fromuid();
	notify["to_uid"] = request->touid();
	notify["chat_id"] = request->chatid();

	// 将通过grpc发送过来的信息转化为json数组
	Json::Value notify_msgs;
	for (auto& msg : request->textmsgs()) {
		Json::Value value;
		value["msg_content"] = msg.msgcontent();
		value["message_id"] = msg.msgid();
        value["msg_uuid"] = msg.uuid();
		notify_msgs.append(value);
	}

	notify["notify_msgs"] = notify_msgs;

	// 通知对方服务器
	std::string notify_str = notify.toStyledString();
	sessions->Send(session, {MSG_NOTIFY_CHAT_MSG_REQ, notify_str});
	return Status::OK;
}

Status ChatServiceImpl::NotifyOtherKickUser(ServerContext* context, const KickUserReq* request, KickUserRsp* reponse) {
	SPDLOG_INFO("notify kick user request, uid={}", request->uid());

	int uid = request->uid();

	// 查询用户是否在本服务器
	auto sessions = UserMgr::GetInstance()->Sessions();
	auto session = sessions->FindCurrent(uid);

	reponse->set_error(ErrorCodes::Success);
	reponse->set_uid(uid);

	// 用户不在内存中，则直接返回
	if (!session) {
		return Status::OK;
	}

	// 在内存中则直接发送通知客户端下线
	// 发送消息通知客户端，由客户端断开链接，不然会出现TIME_OUT
	Json::Value notify;
	notify["error"] = ErrorCodes::Success;
	notify["uid"] = uid;

	std::string return_str = notify.toStyledString();

	sessions->Send(session, {MSG_NOTIFY_OFF_LINE_REQ, return_str});
	//session->NotifyOffline(uid);
	// 清除旧的连接
	_p_server->ClearSession(session);

	return Status::OK;
}

bool ChatServiceImpl::GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& user_info) {
	std::string info_str = "";
	bool success = RedisMgr::GetInstance()->Get(baseinfo_key, info_str);

	// 能够从redis中查询到
	if (success) {
		SPDLOG_DEBUG("redis user base info loaded, uid={}, key={}, value_size={}", uid, baseinfo_key, info_str.size());

		Json::Reader reader;
		Json::Value root;
		int b_parse = reader.parse(info_str, root);
		if (!b_parse) {
			SPDLOG_WARN("redis user base info json parse failed, uid={}, key={}", uid, baseinfo_key);
			return false;
		}
		user_info->_uid = uid;
		user_info->_name = root["name"].asString();
		user_info->_description = root["description"].asString();
		user_info->_icon = root["icon"].asString();
		user_info->_sex = root["sex"].asInt();
	}
	else {
		// 从redis中查询不到，则去mysql中查询
		std::shared_ptr<UserInfo> userinfo = nullptr;
		userinfo = MysqlMgr::GetInstance()->GetUesr(uid);
		if (userinfo == nullptr) {
			return false;
		}

		// 更改返回值
		user_info = userinfo;

		// 准备更新redis数据
		Json::Value redis_root;
		redis_root["uid"] = userinfo->_uid;
		redis_root["description"] = userinfo->_description;
		redis_root["name"] = userinfo->_name;
		redis_root["icon"] = userinfo->_icon;
		redis_root["sex"] = userinfo->_sex;

		// 更新redis
		RedisMgr::GetInstance()->Set(baseinfo_key, redis_root.toStyledString());
	}

	return true;
}
