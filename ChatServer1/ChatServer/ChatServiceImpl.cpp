#include "ChatServiceImpl.h"
#include "UserMgr.h"
#include "const.h"
#include <json/value.h>
#include "CSession.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include <json/reader.h>

ChatServiceImpl::ChatServiceImpl()
{

}

// 别的服务器通知本服务器进行好友申请信息
Status ChatServiceImpl::NotifyOtherAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response) {
	// 查看是否在本服务器，因为有可能已经离线了
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	// 设置返回值
	response->set_error(ErrorCodes::Success);
	response->set_applyuid(request->applyuid());
	response->set_touid(request->touid());

	// 用户会话连接已经断开，用户已下线
	if (session == nullptr) {
		return Status::OK;
	}

	// 在内存中，则直接发送通知
	Json::Value return_value;
	return_value["error"] = ErrorCodes::Success;
	return_value["applyuid"] = request->applyuid();
	return_value["applyname"] = request->applyname();
	return_value["applydescription"] = request->applydescription();
	return_value["applyicon"] = request->applyicon();
	return_value["applysex"] = request->applysex();

	session->Send(return_value.toStyledString(), MSG_NOTIFY_ADD_FRIEND_REQ);

	return Status::OK;
}

// 别的服务器通知本服务其进行认证信息
Status ChatServiceImpl::NotifyOtherAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response) {
	std::cout << "NotifyOtherAuthFriend" << std::endl;
	// 查看是否在本服务器，因为有可能已经离线了
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	// 设置返回值
	response->set_error(ErrorCodes::Success);
	
	// 对方服务器也没有，则用户已下线
	if (session == nullptr) {
		return Status::OK;
	}

	// 当前连接还在，则进行通知
	Json::Value notify;
	notify["error"] = ErrorCodes::Success;
	notify["uid"] = request->fromuid();

	auto fromuid = request->fromuid();
	std::string fromuid_baseinfo_key = USER_BASE_INFO + std::to_string(fromuid);
	auto fromuid_user_info = std::make_shared<UserInfo>();
	bool isSuccess = GetUserBaseInfo(fromuid_baseinfo_key, fromuid, fromuid_user_info);
	//std::cout << "isSuccess : " << isSuccess << std::endl;
	// 因为这是在touid添加fromuid的信息，因此查询的是fromuid的个人信息
	if (isSuccess) {
		notify["name"] = fromuid_user_info->_name;
		notify["description"] = fromuid_user_info->_description;
		notify["icon"] = fromuid_user_info->_icon;
		notify["sex"] = fromuid_user_info->_sex;
	}
	else {
		notify["error"] = ErrorCodes::UidInvalid;
	}

	std::string notify_str = notify.toStyledString();
	session->Send(notify_str, MSG_NOTIFY_AUTH_FRIEND_REQ);
	return Status::OK;
}

// 别的服务器通知接收数据
Status ChatServiceImpl::NotifyOtherReceiveTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response) {
	std::cout << "NotifyOtherReceiveTextChatMsg" << std::endl;

	// 查看是否在本服务器，因为有可能已经离线了
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	// 设置返回值
	response->set_error(ErrorCodes::Success);

	// 对方服务器也没有，则用户已下线
	if (session == nullptr) {
		std::cout << "session == nullptr" << std::endl;
		return Status::OK;
	}

	// 当前连接还在，则进行通知
	Json::Value notify;
	notify["error"] = ErrorCodes::Success;
	notify["from_uid"] = request->fromuid();
	notify["to_uid"] = request->touid();

	// 将通过grpc发送过来的信息转化为json数组
	Json::Value text_array;
	for (auto& msg : request->textmsgs()) {
		Json::Value value;
		value["msg_content"] = msg.msgcontent();
		value["msg_id"] = msg.msgid();
		text_array.append(value);
	}

	notify["text_array"] = text_array;

	// 通知对方服务器
	std::string notify_str = notify.toStyledString();
	session->Send(notify_str, MSG_NOTIFY_CHAT_MSG_REQ);
	return Status::OK;
}

bool ChatServiceImpl::GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& user_info) {
	std::string info_str = "";
	bool success = RedisMgr::GetInstance()->Get(baseinfo_key, info_str);
	//std::cout << info_str << std::endl;
	// 能够从redis中查询到
	if (success) {
		Json::Reader reader;
		Json::Value root;
		int b_parse = reader.parse(info_str, root);
		if (!b_parse) {
			std::cout << "json failure" << std::endl;
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