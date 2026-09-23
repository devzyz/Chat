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

/** @brief 把生成的 Status RPC 接口适配为路由端口，不复制选服与 Token 规则。 */
class StatusServiceImpl final : public StatusService::Service
{
public:
	/** @brief 初始化StatusServiceImpl，把生成的 Status RPC 接口适配为路由端口，不复制选服与 Token 规则。 */
	explicit StatusServiceImpl(StatusRouting& routing);

	/** @brief 把选服请求交给本地路由 Assign，将结果写入响应；业务失败通过响应 error 表达。 */
	Status GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) override;
	/** @brief 把 UID 和 Token 交给本地路由 Validate，将认证结果写入响应。 */
	Status Login(ServerContext* context, const LoginReq* request, LoginRsp* response) override;
private:
	StatusRouting& routing_;
};
