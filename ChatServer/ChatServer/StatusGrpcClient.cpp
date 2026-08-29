#include "StatusGrpcClient.h"

#include "ConfigMgr.h"

StatusGrpcClient::StatusGrpcClient() {
    auto& config = ConfigMgr::GetInstance();
    auto grpc_config = config["Grpc"];
    _policy = {
        rpc::ParseDurationMs(
            grpc_config["PoolAcquireTimeoutMs"],
            "[Grpc].PoolAcquireTimeoutMs",
            std::chrono::milliseconds(1000)),
        rpc::ParseDurationMs(
            grpc_config["StatusDeadlineMs"],
            "[Grpc].StatusDeadlineMs",
            std::chrono::milliseconds(3000))
    };
    _pool = std::make_unique<StatusConnectionPool>(
        config["StatusServer"]["Host"],
        config["StatusServer"]["Port"],
        5,
        _policy.acquire_timeout);
}

StatusGrpcClient::StatusGrpcClient(
    const std::string& host,
    const std::string& port,
    rpc::ClientPolicy policy,
    std::size_t pool_size)
    : _pool(std::make_unique<StatusConnectionPool>(
          host,
          port,
          pool_size,
          policy.acquire_timeout)),
      _policy(policy) {}

LoginRsp StatusGrpcClient::Login(int uid, std::string token) {
    LoginReq request;
    request.set_uid(uid);
    request.set_token(std::move(token));
    auto result = rpc::InvokeUnary<StatusConnectionPool, LoginReq, LoginRsp>(
        *_pool,
        request,
        _policy.rpc_deadline,
        [](StatusService::Stub& stub,
           ClientContext& context,
           const LoginReq& req,
           LoginRsp& rsp) {
            return stub.Login(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}
