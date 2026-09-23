#include "StatusServiceImpl.h"

#include "const.h"

StatusServiceImpl::StatusServiceImpl(StatusRouting& routing)
	: routing_(routing) {
}

/** @brief 把选服请求交给本地路由 Assign，将结果写入响应；业务失败通过响应 error 表达。 */
Status StatusServiceImpl::GetChatServer(
	ServerContext*, const GetChatServerReq* request, GetChatServerRsp* reply) {
	SPDLOG_DEBUG("chat server selection request received, uid={}", request->uid());
	const auto result = routing_.Assign(request->uid());
	reply->set_error(result.error);
	reply->set_host(result.host);
	reply->set_port(result.port);
	reply->set_token(result.token);
	return Status::OK;
}

/** @brief 把 UID 和 Token 交给本地路由 Validate，将认证结果写入响应。 */
Status StatusServiceImpl::Login(
	ServerContext*, const LoginReq* request, LoginRsp* response) {
	const auto result = routing_.Validate(request->uid(), request->token());
	response->set_error(result.error);
	response->set_uid(result.uid);
	response->set_token(result.token);
	return Status::OK;
}
