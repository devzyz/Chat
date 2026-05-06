#include "StatusServiceImpl.h"
#include "const.h"
#include "ConfigMgr.h"
#include <boost/uuid.hpp>

StatusServiceImpl::StatusServiceImpl() {
	auto& configMgr = ConfigMgr::GetInstance();
	ChatServer server;
	server.connection_count = 0;

	server.host = configMgr["ChatServer1"]["Host"];
	server.port = configMgr["ChatServer1"]["Port"];
	server.name = configMgr["ChatServer1"]["Name"];
	_servers[server.name] = server;

	server.host = configMgr["ChatServer2"]["Host"];
	server.port = configMgr["ChatServer2"]["Port"];
	server.name = configMgr["ChatServer2"]["Name"];
	_servers[server.name] = server;
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

	for (const auto& server : _servers) {
		if (server.second.connection_count < minServer.connection_count) {
			minServer = server.second;
		}
	}

	return minServer;
}

/**
 * @brief 
 * @param uid 
 * @param token 
 * 将token插入到map中，做记录
 */
void StatusServiceImpl::insertToken(int uid, std::string token) {
	std::lock_guard<std::mutex> lock(_token_mutex);
	_tokens[uid] = token;
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