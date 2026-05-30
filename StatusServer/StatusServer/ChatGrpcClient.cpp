#include "ChatGrpcClient.h"
#include "ConfigMgr.h"

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

ChatGrpcClient::ChatGrpcClient() {
	auto& configMgr = ConfigMgr::GetInstance();
	auto server_list = configMgr["ChatServers"]["Name"];
	
	std::vector<std::string> names;
	std::stringstream ss(server_list);
	std::string name;

	while (std::getline(ss, name, ',')) {
		names.push_back(name);
	}

	for (auto& name : names) {
		if (configMgr[name]["Name"].empty()) {
			return;
		}
		_pool[configMgr[name]["Name"]] = std::make_unique<ChatConnectionPool>(configMgr[name]["Host"], configMgr[name]["Port"], 2);
	}
}