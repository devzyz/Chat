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
		std::unique_lock<std::mutex> lock(_mutex);

		// 如果逻辑系统没有被关闭，同时队列不为空
		while (_msg_que.empty() && !_b_stop) {
			_cond.wait(lock);
		}

		// 如果逻辑队列关闭，则把所有待处理的依次取出来处理
		if (_b_stop) {
			while (_msg_que.size()) {
				auto msg = _msg_que.front();

				auto call_back_iter = _fun_callbacks.find(msg->_recv_msg_node->_msg_id);

				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					std::cout << "msg id [" << msg->_recv_msg_node->_msg_id << "] handler not found" << std::endl;
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
		std::cout << "recv_msg id is " << msg->_recv_msg_node->_msg_id << std::endl;
		auto call_back_iter = _fun_callbacks.find(msg->_recv_msg_node->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg->_recv_msg_node->_msg_id << "] handler not found" << std::endl;
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
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	// 解析msg_data对应的json数据
	Json::Reader reader;
	Json::Value root;
	auto err = reader.parse(msg_data, root);
	if (err) {
		std::cout << "Json parse failure" << std::endl;
		return;
	}

	auto uid = root["uid"].asInt();
	auto token = root["token"].asString();
	std::cout << "user login uid is " << uid << " user token is " << token << std::endl;

	// 主要目的是查询状态，看是否满足token条件
	auto rsp = StatusGrpcClient::GetInstance()->Login(uid, token);

	Json::Value return_value;
	Defer defer([this, &return_value, session]() {
		std::string return_str = return_value.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		});

	// 如果状态服务查询出错误，则返回
	return_value["error"] = rsp.error();
	if (rsp.error() != ErrorCodes::Success) {
		return;
	}

	// 内存中查询用户信息
	auto find_iter = _users.find(uid);
	std::shared_ptr<UserInfo> user_info = nullptr;

	if (find_iter == _users.end()) {
		user_info = MysqlMgr::GetInstance()->GetUesr(uid);
		if (user_info == nullptr) {
			return_value["error"] = ErrorCodes::UidInvalid;
			return;
		}
		_users[uid] = user_info;
	}
	else {
		user_info = find_iter->second;
	}

	return_value["uid"] = uid;
	return_value["token"] = rsp.token();
	return_value["name"] = user_info->name;
}