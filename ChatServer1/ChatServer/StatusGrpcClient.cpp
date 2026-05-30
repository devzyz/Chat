#include "StatusGrpcClient.h"
#include "ConfigMgr.h"
#include "const.h"

StatusConnectionPool::StatusConnectionPool(const std::string& host, const std::string& port, int poolSize) 
	: _host(host), _port(port), _pool_size(poolSize) {
	try {
		for (int i = 0; i < _pool_size; i++) {
			std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
				grpc::InsecureChannelCredentials());
			_que.push(StatusService::NewStub(channel));
		}
	}
	catch (std::exception& e) {
		std::cout << "create Status Connection Pool is failure, error is " << e.what() << std::endl;
	}
}

StatusConnectionPool::~StatusConnectionPool() {
	std::lock_guard<std::mutex> lock(_que_mutex);
	close();
	while (_que.size()) {
		_que.pop();
	}
}

std::unique_ptr<StatusService::Stub> StatusConnectionPool::getConnection() {
	std::unique_lock<std::mutex> lock(_que_mutex);
	_cond.wait(lock, [this]() {
		if (_b_stop) {
			return true;
		}
		return !_que.empty();
		});
	if (_b_stop) {
		return nullptr;
	}
	auto con = std::move(_que.front());
	_que.pop();
	return con;
}

void StatusConnectionPool::returnConnection(std::unique_ptr<StatusService::Stub> connection) {
	std::lock_guard<std::mutex> lock(_que_mutex);
	if (_b_stop) {
		return;
	}
	_que.push(std::move(connection));
	_cond.notify_one();
}

void StatusConnectionPool::close() {
	_b_stop = true;
	_cond.notify_all();
}

StatusGrpcClient::~StatusGrpcClient() {
	std::cout << "StatusGrpcClient destructed." << std::endl;
}

StatusGrpcClient::StatusGrpcClient() {
	auto& configMgr = ConfigMgr::GetInstance();

	std::string host = configMgr["StatusServer"]["Host"];
	std::string port = configMgr["StatusServer"]["Port"];

	_pool.reset(new StatusConnectionPool(host, port, 3));
}

LoginRsp StatusGrpcClient::Login(int uid, std::string token) {
	ClientContext context;
	LoginRsp reply;
	LoginReq request;
	request.set_uid(uid);
	request.set_token(token);

	auto stub = _pool->getConnection();
	Status status = stub->Login(&context, request, &reply);

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