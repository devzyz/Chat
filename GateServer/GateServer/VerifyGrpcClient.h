#pragma once
#include <grpcpp/grpcpp.h>
#include "const.h"
#include "Singleton.h"
#include "message.grpc.pb.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::VarifyService;

class RPCConnectionPool {
public : 
	RPCConnectionPool(std::size_t poolsize, std::string host, std::string port);

	~RPCConnectionPool();
	
	// 在析构时，通知所有其他线程，当前池子已经被析构了
	void Close();

	// unique_lock<std::mutex> 可手动解锁，可以搭配condition_variable使用
	// lock_guard<std::mutex> 不能够手动解锁

	// 从池子里取数据
	std::unique_ptr<VarifyService::Stub> getConnection();

	// 将数据归还给池子
	void returnConnection(std::unique_ptr<VarifyService::Stub> context);

private:
	std::atomic<bool> _b_stop;
	std::size_t _poolSize;
	std::string _host;
	std::string _port;
	std::queue<std::unique_ptr<VarifyService::Stub>> _connections;
	std::condition_variable _cond;
	std::mutex _mutex;
};

class VerifyGrpcClient : public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;
public:
	GetVarifyRsp GetVarifyCode(std::string email);

private:
	VerifyGrpcClient();

	std::unique_ptr<RPCConnectionPool> _pool;
};

