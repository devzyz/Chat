#include "StatusServiceImpl.h"
#include "const.h"
#include "ConfigMgr.h"
#include <boost/uuid.hpp>
#include "RedisMgr.h"

/**
 * @brief 
 * 读取所有的ChatServer服务器信息
 */
StatusServiceImpl::StatusServiceImpl() {
	auto& configMgr = ConfigMgr::GetInstance();
	auto server_list = configMgr["ChatServers"]["Name"];

	std::vector <std::string> names;
	std::stringstream ss(server_list);
	std::string name;

	while (std::getline(ss, name, ',')) {
		names.push_back(name);
	}

	for (auto& name : names) {
		if (configMgr[name]["Name"].empty()) {
			return;
		}

		ChatServer chatserver;
		chatserver.host = configMgr[name]["Host"];
		chatserver.port = configMgr[name]["Port"];
		chatserver.name = configMgr[name]["Name"];
		_servers[chatserver.name] = chatserver;
	}
}

/**
 * @brief 
 * @return
 * 生成token
 */
std::string generate_unique_string() {
	// 创建UUID对象
	boost::uuids::uuid uuid = boost::uuids::random_generator()();
	// 将UUID转换为字符串
	std::string unique_string = to_string(uuid);
	return unique_string;
}


/**
 * @brief 
 * @return
 * 找到负载最小的服务器
 */
ChatServer StatusServiceImpl::getChatServer() {
	std::lock_guard<std::mutex> lock(_server_mutex);
	auto minServer = _servers.begin()->second;
	std::string count_str;
	// 去redis查询某个chatserver的tcp客户端连接数
	RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name, count_str);
	// 没找到，则默认负载最大
	if (count_str.empty()) {
		minServer.connection_count = INT_MAX;
	}
	else {
		minServer.connection_count = std::stoi(count_str);
	}

	// 通过for循环，依次枚举所有的chatserver找到tcp连接数最少的
	for (auto& server : _servers) {
		if (server.second.name == minServer.name) {
			continue;
		}

		RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name, count_str);
		if (count_str.empty()) {
			server.second.connection_count = INT_MAX;
		}
		else {
			server.second.connection_count = std::stoi(count_str);
		}

		if (minServer.connection_count > server.second.connection_count) {
			minServer = server.second;
		}
	}

	return minServer;
}

/**
 * @brief 
 * @param uid 
 * @param token 
 * 将token插入到redis中
 */
void StatusServiceImpl::insertToken(int uid, std::string token) {
	std::string uid_str = std::to_string(uid);
	std::string token_key = USER_TOKEN_PREFIX + uid_str;
	RedisMgr::GetInstance()->HSet(uid_str, token_key, token);
}

/**
 * @brief 
 * @param ontext 
 * @param request 
 * @param reply 
 * @return 
 * 获取负载最小的服务器
 * 
 * 重写的grpc方法，客户端实际希望调用的函数就是这个函数
 */
Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) {
	std::cout << "status server has received : " << std::endl;

	const auto& server = getChatServer();

	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(generate_unique_string());
	insertToken(request->uid(), reply->token());
	return Status::OK;
}

/**
 * @brief 
 * @param context 
 * @param request 
 * @param response 
 * @return 
 * 对登录服务器的token进行校验
 */
Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* response)
{
	auto uid = request->uid();
	auto token = request->token();
	
	std::string uid_str = std::to_string(uid);
	std::string token_key = USER_TOKEN_PREFIX + uid_str;
	std::string token_value = "";
	bool success = RedisMgr::GetInstance()->HGet(uid_str, token_key, token_value);

	if (!success) {
		response->set_error(ErrorCodes::UidInvalid);
		return Status::OK;
	}

	if (token_value != token) {
		response->set_error(ErrorCodes::TokenInvalid);
		return Status::OK;
	}

	response->set_error(ErrorCodes::Success);
	response->set_uid(uid);
	response->set_token(token);

	return Status::OK;
}
