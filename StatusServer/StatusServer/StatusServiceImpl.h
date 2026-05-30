#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using message::GetChatServerReq;
using message::GetChatServerRsp;

using message::LoginReq;
using message::LoginRsp;

using message::StatusService;

struct ChatServer {
	std::string host;
	std::string port;
	std::string name;
	int connection_count;
};

class StatusServiceImpl final : public StatusService::Service
{
public:
	StatusServiceImpl();

	Status GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) override;
	Status Login(ServerContext* context, const LoginReq* request, LoginRsp* response) override;
private:
	ChatServer getChatServer();
	void insertToken(int uid, std::string token);
	// 保存所有的启用的ChatServer服务器的信息
	std::unordered_map<std::string, ChatServer> _servers;
	std::mutex _server_mutex;
};

