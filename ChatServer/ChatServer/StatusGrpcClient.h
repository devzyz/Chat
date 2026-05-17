#pragma once
#include "Singleton.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <grpcpp/grpcpp.h>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <memory>

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

/**
 * @brief 
 * 状态服务连接池
 */
class StatusConnectionPool {
public:
	StatusConnectionPool(const std::string& host, const std::string& port, int poolSize);
	~StatusConnectionPool();
	std::unique_ptr<StatusService::Stub> getConnection();
	void returnConnection(std::unique_ptr<StatusService::Stub> connection);
	void close();
private:
	std::atomic<bool> _b_stop;
	std::queue<std::unique_ptr<StatusService::Stub>> _que;

	// 条件变量，为了实现生产者消费者模型
	std::condition_variable _cond;
	// 互斥访问队列的信号量
	std::mutex _que_mutex;

	// 连接池大小
	int _pool_size;
	std::string _host;
	std::string _port;
};

class StatusGrpcClient : public Singleton<StatusGrpcClient>
{
	friend class Singleton<StatusGrpcClient>;
public:
	~StatusGrpcClient();
	LoginRsp Login(int uid, std::string token);

private:
	StatusGrpcClient();
	std::unique_ptr<StatusConnectionPool> _pool;
};

