#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include <string.h>
#include <sstream>
#include "const.h"

ChatConnectionPool::ChatConnectionPool(const std::string& host, const std::string& port, std::size_t poolSize) 
	: _host(host), _port(port), _pool_size(poolSize), _b_stop(false) {
	for (std::size_t i = 0; i < _pool_size; i++) {
		std::shared_ptr<Channel> channel = grpc::CreateChannel(_host + ":" + _port, grpc::InsecureChannelCredentials());
		_connection.push(ChatService::NewStub(channel));
	}
}

ChatConnectionPool::~ChatConnectionPool() {
	std::lock_guard<std::mutex> lock(_que_mutex);
	close();
	while (_connection.size()) {
		_connection.pop();
	}
}

void ChatConnectionPool::returnConnectioni(std::unique_ptr<ChatService::Stub> connection) {
	std::lock_guard<std::mutex> lock(_que_mutex);
	if (_b_stop) {
		return;
	}
	_connection.push(std::move(connection));
	_cond.notify_one();
}

std::unique_ptr<ChatService::Stub> ChatConnectionPool::getConnection() {
	std::unique_lock<std::mutex> lock(_que_mutex);
	_cond.wait(lock, [this]() {
		if (_b_stop) {
			return true;
		}
		return !_connection.empty();
		});

	if (_b_stop) {
		return nullptr;
	}

	auto connection = std::move(_connection.front());
	_connection.pop();
	return connection;
}

void ChatConnectionPool::close() {
	_b_stop = true;
	_cond.notify_all();
}

ChatGrpcClient::~ChatGrpcClient() {

}

/**
 * @brief 
 * 因为可能有多个聊天服务器，因此需要读取到所有的PeerServer服务器，并与每个服务器建立grpc通信
 */
ChatGrpcClient::ChatGrpcClient() {
	auto& configMgr = ConfigMgr::GetInstance();

	auto server_list = configMgr["PeerServer"]["Servers"];

	std::vector<std::string> servers;
	std::stringstream ss(server_list);
	std::string server;
	while (std::getline(ss, server, ',')) {
		servers.push_back(server);
	}

	for (auto& server : servers) {
		if (configMgr[server]["Name"].empty()) {
			continue;
		}
		_pool[configMgr[server]["Name"]] = std::make_unique<ChatConnectionPool>(configMgr[server]["Host"], configMgr[server]["Port"], 2);
	}
}

/**
 * @brief 
 * @param serverIp 
 * @param req 
 * @return 
 * 申请添加好友时，如果好友不在本服务器，则需通过grpc通知另一个服务器
 */
AddFriendRsp ChatGrpcClient::NotifyOtherAddFriend(const std::string& serverIp, const AddFriendReq req) {
	AddFriendRsp rsp;
	rsp.set_error(ErrorCodes::Success);
	rsp.set_applyuid(req.applyuid());
	rsp.set_touid(req.touid());

	auto find_iter = _pool.find(serverIp);
	if (find_iter == _pool.end()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto connection = pool->getConnection();
	if (connection == nullptr) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	Defer defer([&pool, &connection]() {
		pool->returnConnectioni(std::move(connection));
		});

	Status status = connection->NotifyOtherAddFriend(&context, req, &rsp);
	
	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}

// 通知serverIp服务器，认证好友
AuthFriendRsp ChatGrpcClient::NotifyOtherAuthFriend(const std::string& serverIp, const AuthFriendReq request) {
	AuthFriendRsp rsp;
	rsp.set_error(ErrorCodes::Success);

	// 如果找不到服务器，则返回
	auto iter_find = _pool.find(serverIp);
	if (iter_find == _pool.end()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	// 先取出对应这个服务器的连接池
	auto& pool = iter_find->second;
	ClientContext context;
	// 从连接池里面取一个连接
	auto connection = pool->getConnection();
	if (connection == nullptr) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	Defer defer([&pool, &connection]() {
		pool->returnConnectioni(std::move(connection));
		});
	
	// 调用grpc服务
	Status status = connection->NotifyOtherAuthFriend(&context, request, &rsp);

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}

TextChatMsgRsp ChatGrpcClient::NotifyOtherReceiveTextChatMsg(const std::string& serverIp, const TextChatMsgReq request) {
	TextChatMsgRsp rsp;
	rsp.set_error(ErrorCodes::Success);

	// 如果找不到服务器，则返回
	auto iter_find = _pool.find(serverIp);
	if (iter_find == _pool.end()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	// 取出连接这个服务的grpc连接池
	auto& pool = iter_find->second;
	ClientContext context;

	// 取出一个连接
	auto connection = pool->getConnection();
	if (connection == nullptr) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	Defer defer([&pool, &connection]() {
		pool->returnConnectioni(std::move(connection));
		});

	// 调用grpc服务
	Status status = connection->NotifyOtherReceiveTextChatMsg(&context, request, &rsp);
	
	if(!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}