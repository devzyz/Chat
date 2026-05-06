#include "StatusGrpcClient.h"
#include "ConfigMgr.h"

RPCConnection::RPCConnection(std::size_t poolsize, std::string host, std::string port) :
	_poolSize(poolsize), _host(host), _port(port), _b_stop(false) {
	for (std::size_t i = 0; i < _poolSize; i++) {
		// 创建连接通道
		std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
			grpc::InsecureChannelCredentials());
		// 创建通道管理员
		_connections.push(StatusService::NewStub(channel));
	}
}

RPCConnection::~RPCConnection() {
	std::unique_lock<std::mutex> lock(_mutex);
	close();
	while (!_connections.empty()) {
		_connections.pop();
	}
}

void RPCConnection::close() {
	_b_stop = false;
	_cond.notify_all();
}

std::unique_ptr<StatusService::Stub> RPCConnection::getConnection() {
	std::unique_lock<std::mutex> lock(_mutex);
	_cond.wait(lock, [this]() {
		if (_b_stop) {
			return true;
		}
		return !_connections.empty();
		});

	if (_b_stop) {
		return nullptr;
	}

	// 返回队首
	auto con = std::move(_connections.front());
	_connections.pop();
	return con;
}

void RPCConnection::returnConnection(std::unique_ptr<StatusService::Stub> context) {
	std::unique_lock<std::mutex> lock(_mutex);
	if (!_b_stop) {
		return;
	}
	_connections.push(std::move(context));
	_cond.notify_one();
}

StatusGrpcClient::StatusGrpcClient() {
	auto& configMgr = ConfigMgr::GetInstance();
	std::string host = configMgr["StatusServer"]["Host"];
	std::string port = configMgr["StatusServer"]["Port"];
	_pool.reset(new RPCConnection(5, host, port));
}

StatusGrpcClient::~StatusGrpcClient() {

}

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid) {
	ClientContext context;
	GetChatServerRsp reply;
	GetChatServerReq request;
	request.set_uid(uid);

	auto stub = _pool->getConnection();
	Status status = stub->GetChatServer(&context, request, &reply);

	Defer defer([this, &stub]() {
		_pool->returnConnection(std::move(stub));
		});

	if (status.ok()) {
		return reply;
	}
	else {
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
}
