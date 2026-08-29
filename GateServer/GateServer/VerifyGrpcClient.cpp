#include "VerifyGrpcClient.h"

#include "ConfigMgr.h"

GetVarifyRsp VerifyGrpcClient::GetVarifyCode(std::string email) {
    GetVarifyReq request;
    request.set_email(email);

    auto result = rpc::InvokeUnary<RPCConnectionPool, GetVarifyReq, GetVarifyRsp>(
        *_pool,
        request,
        _policy.rpc_deadline,
        [](VarifyService::Stub& stub,
           ClientContext& context,
           const GetVarifyReq& req,
           GetVarifyRsp& rsp) {
            return stub.GetVarifyCode(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}

VerifyGrpcClient::VerifyGrpcClient() {
    auto& config = ConfigMgr::GetInstance();
    auto grpc_config = config["Grpc"];
    _policy = {
        rpc::ParseDurationMs(
            grpc_config["PoolAcquireTimeoutMs"],
            "[Grpc].PoolAcquireTimeoutMs",
            std::chrono::milliseconds(1000)),
        rpc::ParseDurationMs(
            grpc_config["VarifyDeadlineMs"],
            "[Grpc].VarifyDeadlineMs",
            std::chrono::milliseconds(15000))
    };
    _pool = std::make_unique<RPCConnectionPool>(
        5,
        config["VarifyServer"]["Host"],
        config["VarifyServer"]["Port"],
        _policy.acquire_timeout);
}

VerifyGrpcClient::VerifyGrpcClient(
    const std::string& host,
    const std::string& port,
    rpc::ClientPolicy policy,
    std::size_t pool_size)
    : _pool(std::make_unique<RPCConnectionPool>(
          pool_size,
          host,
          port,
          policy.acquire_timeout)),
      _policy(policy) {}
