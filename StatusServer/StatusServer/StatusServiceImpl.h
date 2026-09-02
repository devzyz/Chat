#pragma once
#include <grpcpp/grpcpp.h>
#include "status.grpc.pb.h"
#include "StatusRouting.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using message::GetChatServerReq;
using message::GetChatServerRsp;

using message::LoginReq;
using message::LoginRsp;

using message::StatusService;

class StatusServiceImpl final : public StatusService::Service
{
public:
	StatusServiceImpl();

	Status GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) override;
	Status Login(ServerContext* context, const LoginReq* request, LoginRsp* response) override;
private:
	std::unique_ptr<StatusRouting> routing_;
};
