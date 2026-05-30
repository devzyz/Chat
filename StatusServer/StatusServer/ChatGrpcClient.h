#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "Singleton.h"
#include <unordered_map>

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::ChatService;

class ChatConnectionPool {
public:
	ChatConnectionPool(const std::string& _host, const std::string& port, std::size_t poolSize);
	~ChatConnectionPool();
	void returnConnectioni(std::unique_ptr<ChatService::Stub>);
	std::unique_ptr<ChatService::Stub> getConnection();
	void close();
private:
	std::atomic<bool> _b_stop;
	std::size_t _pool_size;
	std::string _host;
	std::string _port;
	std::queue<std::unique_ptr<ChatService::Stub>> _connection;

	std::mutex _que_mutex;
	std::condition_variable _cond;
};
class ChatGrpcClient : public Singleton<ChatGrpcClient>
{
	friend class Singleton<ChatGrpcClient>;
public:
	~ChatGrpcClient();
private:
	ChatGrpcClient();
	std::unordered_map<std::string, std::unique_ptr<ChatConnectionPool>> _pool;
};

