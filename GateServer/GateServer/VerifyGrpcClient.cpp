#include "VerifyGrpcClient.h"
#include "ConfigMgr.h"

RPCConnectionPool::RPCConnectionPool(std::size_t poolsize, std::string host, std::string port) : 
	_poolSize(poolsize), _host(host), _port(port), _b_stop(false) {
	for (std::size_t i = 0; i < _poolSize; i++) {
		std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
			grpc::InsecureChannelCredentials());

		_connections.push(VarifyService::NewStub(channel));
	}
}

RPCConnectionPool::~RPCConnectionPool() {
	// 互斥访问queue
	std::lock_guard<std::mutex> lock(_mutex);
	Close();
	while (!_connections.empty()) {
		_connections.pop();
	}
}

// 在析构时，通知所有其他线程，当前池子已经被析构了
void RPCConnectionPool::Close() {
	_b_stop = true;
	_cond.notify_all();
}

// 从池子里取数据
std::unique_ptr<VarifyService::Stub> RPCConnectionPool::getConnection() {
	std::unique_lock<std::mutex> lock(_mutex);
	// wait 当lambda函数返回true时，继续往下走，否则则会将lock解锁，并等待，直到能够使得lambda为true
	_cond.wait(lock, [this]() {
		if (_b_stop) {
			return true;
		}
		return !_connections.empty();
		});

	// 如果已经停止服务，则返回空
	if (_b_stop) {
		return nullptr;
	}

	// 否则返回队首
	auto context = std::move(_connections.front());
	_connections.pop();
	return context;
}

// 将数据归还给池子
void RPCConnectionPool::returnConnection(std::unique_ptr<VarifyService::Stub> context) {
	std::lock_guard<std::mutex> lock(_mutex);
	if (_b_stop) {
		return;
	}
	_connections.push(std::move(context));
	_cond.notify_one();
}

GetVarifyRsp VerifyGrpcClient::GetVarifyCode(std::string email) {
	ClientContext context;
	GetVarifyRsp reply;
	GetVarifyReq request;
	request.set_email(email);

	auto stub = _pool->getConnection();
	Status status = stub->GetVarifyCode(&context, request, &reply);

	if (status.ok()) {
		_pool->returnConnection(std::move(stub));
		return reply;
	}
	else {
		_pool->returnConnection(std::move(stub));
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
}

VerifyGrpcClient::VerifyGrpcClient() {
	auto& configMgr = ConfigMgr::GetInstance();
	std::string host = configMgr["VarifyServer"]["Host"];
	std::string port = configMgr["VarifyServer"]["Port"];
	_pool .reset(new RPCConnectionPool(5, host, port));
}