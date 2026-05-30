#include "LogicSystem.h"
#include "CSession.h"
#include "const.h"
#include <Json/Json.h>
#include <Json/reader.h>
#include <Json/value.h>
#include "data.h"
#include <memory>
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "UserMgr.h"
#include "ChatGrpcClient.h"

LogicSystem::LogicSystem() : _b_stop(false) {
	RegisterCallBacks();
	_worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem() {
	_b_stop = true;
	_cond.notify_one();
	_worker_thread.join();
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
	std::unique_lock<std::mutex> lock(_mutex);

	if (_msg_que.size() > MAX_DEALQUE) {
		std::cout << "Failed, deal queue is full, size is " << MAX_DEALQUE << std::endl;
		return;
	}

	_msg_que.push(msg);
	// 由0变为1的时候，通知工作线程不要挂起了
	if (_msg_que.size() == 1) {
		lock.unlock();
		_cond.notify_one();
	}
}
/**
 * @brief 
 * 工作进程运行的函数
 */
void LogicSystem::DealMsg() {
	for (;;) {
		// 保证对queue的加锁访问
		std::unique_lock<std::mutex> lock(_mutex);

		// 如果逻辑系统没有被关闭，同时队列不为空，同时可以防止虚假唤醒。lambda为true时，继续往下执行
		_cond.wait(lock, [this]() { 
			return !_msg_que.empty() || _b_stop;
			});

		// 如果逻辑队列关闭，则把所有待处理的依次取出来处理
		if (_b_stop) {
			while (_msg_que.size()) {
				auto msg = _msg_que.front();

				auto call_back_iter = _fun_callbacks.find(msg->_recv_msg_node->_msg_id);

				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					std::cout << "msg id [" << msg->_recv_msg_node->_msg_id << "] handler not found" << std::endl; // 日志todo...
					continue;
				}
				call_back_iter->second(msg->_session, msg->_recv_msg_node->_msg_id,
					std::string(msg->_recv_msg_node->_data, msg->_recv_msg_node->_cur_len));
				_msg_que.pop();
			}

			break;
		}

		// 未关闭，则取出队首进行处理
		auto msg = _msg_que.front();
		std::cout << "recv_msg id is " << msg->_recv_msg_node->_msg_id << std::endl; // 日志todo...
		auto call_back_iter = _fun_callbacks.find(msg->_recv_msg_node->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg->_recv_msg_node->_msg_id << "] handler not found" << std::endl; // 日志todo...
			continue;
		}
		call_back_iter->second(msg->_session, msg->_recv_msg_node->_msg_id,
			std::string(msg->_recv_msg_node->_data));
		_msg_que.pop();
	}
}

/**
 * @brief 
 * 回调函数注册位置
 */
void LogicSystem::RegisterCallBacks() {
	// 处理用户登录请求
	_fun_callbacks[MSG_CHAT_LOGIN] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
		// 解析msg_data对应的json数据
		Json::Reader reader;
		Json::Value root;
		auto err = reader.parse(msg_data, root);
		if (!err) {
			std::cout << "Json parse failure" << std::endl;
			return;
		}

		int uid = root["uid"].asInt();
		std::string token = root["token"].asString();
		std::cout << "user login uid is " << uid << " user token is " << token << std::endl;

		// 主要目的是查询状态，看是否满足token条件
		auto rsp = StatusGrpcClient::GetInstance()->Login(uid, token);

		Json::Value return_value;
		// 自动返回函数，当函数执行到右括号后，局部变量会被析构，此时defer被析构，其析构时，回调用lambda函数
		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_CHAT_LOGIN_RSP);
			});

		// 如果状态服务查询出错误，则返回
		return_value["error"] = rsp.error();
		if (rsp.error() != ErrorCodes::Success) {
			return;
		}

		// 查询用户的基本信息，如果redis中没有，则去mysql中查询
		std::string baseinfo_key = USER_BASE_INFO + std::to_string(uid);
		auto user_info = std::make_shared<UserInfo>();
		bool success = GetUserBaseInfo(baseinfo_key, uid, user_info);
		if (!success) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
		}

		return_value["error"] = ErrorCodes::Success;
		return_value["uid"] = user_info->_uid;
		return_value["name"] = user_info->_name;
		return_value["description"] = user_info->_description;
		return_value["icon"] = user_info->_icon;
		return_value["email"] = user_info->_email;
		return_value["sex"] = user_info->_sex;
		return_value["token"] = token;

		// 获取好友申请列表
		std::vector<std::shared_ptr<ApplyInfo>> apply_list;
		auto b_apply = GetApplyFriendList(uid, apply_list);
		if (b_apply) {
			for (auto& info : apply_list) {
				Json::Value apply;
				apply["name"] = info->_name;
				apply["description"] = info->_description;
				apply["sex"] = info->_sex;
				apply["status"] = info->_status;
				apply["icon"] = info->_icon;
				apply["uid"] = info->_uid;
				return_value["apply_list"].append(apply);
			}
		}

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
				return_value["friend_list"].append(friends);
			}
		}

		// 完成登录后，需要对redis中保存的每个chatserver服务器的连接数加1
		auto self_server_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];

		std::string count_str = "";
		int count = 0;
		RedisMgr::GetInstance()->HGet(LOGIN_COUNT, self_server_name, count_str);

		if (count_str != "") {
			count = std::stoi(count_str);
		}
		count++;
		count_str = std::to_string(count);
		RedisMgr::GetInstance()->HSet(LOGIN_COUNT, self_server_name, count_str);

		// 连接完成后，将session绑定uid
		session->SetUserId(uid);

		// 连接完成后，将userMgr也绑定session
		UserMgr::GetInstance()->SetUserSession(uid, session);

		// 如果需要跟其他的用户通信，其他的用户可能在其他服务器上，因此需要知道每个tcp客户端登陆在哪一个服务器上
		std::string user_server_key = USER_IP_PREFIX + std::to_string(uid);
		RedisMgr::GetInstance()->Set(user_server_key, self_server_name);
	};

	// 处理搜索用户的请求
	_fun_callbacks[MSG_SEARCH_USER_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) -> void{
		// 解析msg_data对应的json数据
		Json::Reader reader;
		Json::Value root;
		auto err = reader.parse(msg_data, root);
		if (!err) {
			std::cout << "Json parse failure" << std::endl;
			return;
		}

		auto uid_name = root["uid_name"].asString();
		std::cout << "user search uid_name is " << uid_name << std::endl;

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
			std::cout << "Json parse failure" << std::endl;
			return;
		}

		auto uid = root["uid"].asInt(); // 申请人
		auto applyname = root["applyname"].asString(); // 申请人名字
		auto backname = root["backname"].asString(); // 申请人给touid的备注名
		auto touid = root["touid"].asInt(); // 申请添加touid为好友

		std::cout << "Json is : uid is " << uid << ", applyname is " << applyname << ", backname is " << backname
			<< ", touid is " << touid << std::endl;

		Json::Value return_value;
		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_ADD_FRIEND_RSP);
			});

		// 先更新数据库
		MysqlMgr::GetInstance()->AddFriendApply(uid, touid);

		// 接下来查找touid连接到哪一个服务器上，如果在本服务器，则直接转发，否则通过grpc转发
		// 如果查不到，代表touid已离线，则直接返回
		auto to_uid_str = std::to_string(touid);
		auto to_uid_ip_key = USER_IP_PREFIX + to_uid_str;
		std::string to_ip_value = "";
		bool b_to_ip = RedisMgr::GetInstance()->Get(to_uid_ip_key, to_ip_value);
		if (!b_to_ip) {
			return;
		}

		// 接下来判断是否跟自己在同一份服务器
		auto& configMgr = ConfigMgr::GetInstance();
		auto self_name = configMgr["SelfServer"]["Name"];

		// 查询到在同一服务器
		if (to_ip_value == self_name) {
			auto touid_session = UserMgr::GetInstance()->GetSession(touid); // usermgr中保存了所有连接本服务器的session
			if (touid_session) {
				// 直接通知对方
				Json::Value notify;
				notify["error"] = ErrorCodes::Success;
				notify["applyuid"] = uid;
				notify["applyname"] = applyname;
				std::string notity_str = notify.toStyledString();
				touid_session->Send(notity_str, MSG_NOTIFY_ADD_FRIEND_REQ);
			}
			return;
		}

		// 查询一下自己的信息
		std::string baseinfo_key = USER_BASE_INFO + std::to_string(uid);
		std::shared_ptr<UserInfo> user_info = std::make_shared<UserInfo> ();
		bool b_info = GetUserBaseInfo(baseinfo_key, uid, user_info);

		// 通过grpc通知另一个服务器添加好友
		AddFriendReq req;
		req.set_applyuid(uid);
		req.set_applyname(applyname);
		req.set_touid(touid);
		if (b_info) {
			req.set_applydescription(user_info->_description);
			req.set_applyicon(user_info->_icon);
			req.set_applysex(user_info->_sex);
		}

		// 发送grpc请求
		ChatGrpcClient::GetInstance()->NotifyOtherAddFriend(to_ip_value, req);
	};

	// 处理认证好友的请求
	_fun_callbacks[MSG_AUTH_FRIEND_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
		std::cout << "MST_AUTH_FRIEND_REQ : " << MSG_AUTH_FRIEND_REQ << std::endl;
		// 解析json数据
		Json::Reader reader;
		Json::Value root;
		auto err = reader.parse(msg_data, root);
		if (!err) {
			std::cout << "Json parse failure" << std::endl;
			return;
		}

		auto fromuid = root["fromuid"].asInt(); // 申请人
		auto backname = root["backname"].asString(); // 申请人给touid的备注名
		auto touid = root["touid"].asInt(); // 申请添加touid为好友

		Json::Value return_value;
		
		// 获取对方的信息
		std::string touid_baseinfo_key = USER_BASE_INFO + std::to_string(touid);
		auto touid_user_info = std::make_shared<UserInfo>();
		bool b_info = GetUserBaseInfo(touid_baseinfo_key, touid, touid_user_info);
		if (b_info) {
			return_value["error"] = ErrorCodes::Success;
			return_value["name"] = touid_user_info->_name;
			return_value["description"] = touid_user_info->_description;
			return_value["sex"] = touid_user_info->_sex;
			return_value["icon"] = touid_user_info->_icon;
			return_value["uid"] = touid_user_info->_uid;
		}
		else {
			return_value["error"] = ErrorCodes::UidInvalid;
		}

		// 发送回包
		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_AUTH_FRIEND_RSP);
			});

		// 更新数据库
		MysqlMgr::GetInstance()->UpdateFriendAuth(fromuid, touid, backname);

		// 通知对方认证成功
		// 先查询redis，查看对方的server_ip
		auto touid_str = std::to_string(touid);
		auto touid_ip_key = USER_IP_PREFIX + touid_str;
		std::string touid_ip_value = "";
		bool isSuccess = RedisMgr::GetInstance()->Get(touid_ip_key, touid_ip_value);
		// 查询不到，说明对方已离线，则不对界面进行更新。因为已经对数据库更新了，因此下次登录是正确结果
		if (!isSuccess) {
			return;
		}

		// 拿出自己的server_ip查看是否相同
		auto& configMgr = ConfigMgr::GetInstance();
		auto self_server_name = configMgr["SelfServer"]["Name"];

		// 两个人在同一个服务器上，则直接找到对方的session，并发送请求
		if (self_server_name == touid_ip_value) {
			auto session = UserMgr::GetInstance()->GetSession(touid);
			if (session) {
				Json::Value notify;
				notify["fromuid"] = fromuid;
				notify["touid"] = touid;
				notify["error"] = ErrorCodes::Success;

				// 查询出我自己的信息，附带发给对方
				std::string fromuid_baseinfo_key = USER_BASE_INFO + std::to_string(fromuid);
				auto fromuid_user_info = std::make_shared<UserInfo>();
				bool isSuccess = GetUserBaseInfo(fromuid_baseinfo_key, fromuid, fromuid_user_info);
				if (isSuccess) {
					notify["name"] = fromuid_user_info->_name;
					notify["sex"] = fromuid_user_info->_sex;
					notify["description"] = fromuid_user_info->_description;
					notify["icon"] = fromuid_user_info->_icon;
				}
				else {
					notify["error"] = ErrorCodes::UidInvalid;
				}

				// 通过session发送
				std::string notify_str = notify.toStyledString();
				session->Send(notify_str, MSG_NOTIFY_AUTH_FRIEND_REQ); 
			}

			return;
		}

		// 两个人不在同一个服务器上，通过grpc发送请求
		AuthFriendReq auth_req;
		auth_req.set_fromuid(fromuid);
		auth_req.set_touid(touid);
		ChatGrpcClient::GetInstance()->NotifyOtherAuthFriend(touid_ip_value, auth_req);
	};

	// 处理用户发送的文本请求
	_fun_callbacks[MSG_TEXT_CHAT_MSG_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
		std::cout << "MSG_TEXT_CHAT_MSG_REQ : " << MSG_TEXT_CHAT_MSG_REQ << std::endl;
		// 解析json数据
		Json::Reader reader;
		Json::Value root;
		auto err = reader.parse(msg_data, root);
		if (!err) {
			std::cout << "Json parse failure" << std::endl;
			return;
		}

		auto from_uid = root["from_uid"].asInt();
		auto to_uid = root["to_uid"].asInt();

		const Json::Value data_array = root["text_array"];

		Json::Value return_value;
		return_value["error"] = ErrorCodes::Success;
		return_value["from_uid"] = from_uid;
		return_value["to_uid"] = to_uid;
		return_value["text_array"] = data_array;

		Defer defer([this, &return_value, session]() {
			std::string return_str = return_value.toStyledString();
			session->Send(return_str, MSG_TEXT_CHAT_MSG_RSP);
			});

		// 查询redis查看对方的ip
		std::string touid_ip_key = USER_IP_PREFIX + std::to_string(to_uid);
		std::string touid_ip_value = "";
		bool b_success = RedisMgr::GetInstance()->Get(touid_ip_key, touid_ip_value);
		if (!b_success) {
			return;
		}

		auto& configMgr = ConfigMgr::GetInstance();
		auto self_server_name = configMgr["SelfServer"]["Name"];

		// 两者在同一个服务器，则直接发送
		if (self_server_name == touid_ip_value) {
			auto session = UserMgr::GetInstance()->GetSession(to_uid);
			if (session) {
				// 直接在这里通知
				std::string return_str = return_value.toStyledString();
				session->Send(return_str, MSG_NOTIFY_CHAT_MSG_REQ);
			}
			
			return;
		}

		// 两者不在同一个服务器，则通过grpc发送
		TextChatMsgReq send_req;
		send_req.set_fromuid(from_uid);
		send_req.set_touid(to_uid);
		for (const auto& text_obj : data_array) {
			auto msg_content = text_obj["msg_content"].asString();
			auto msg_id = text_obj["msg_id"].asString();
			// 创建多个内部的变量
			auto * text_msg = send_req.add_textmsgs();
			text_msg->set_msgid(msg_id);
			text_msg->set_msgcontent(msg_content);
		}
		ChatGrpcClient::GetInstance()->NotifyOtherReceiveTextChatMsg(touid_ip_value, send_req);
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

		std::cout << "search user : uid " << uid << ", name is " << name << ", description is " << description <<
			", icon is " << icon << ", sex is " << sex << std::endl;
		
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

		std::cout << "search user : uid " << uid << ", name is " << name << ", description is " << description <<
			", icon is " << icon << ", sex is " << sex << std::endl;

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