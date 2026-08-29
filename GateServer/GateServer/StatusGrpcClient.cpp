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
    _pool = std::make_unique<RPCConnection>(
        5,
        config["StatusServer"]["Host"],
        config["StatusServer"]["Port"],
        _policy.acquire_timeout);
}

StatusGrpcClient::StatusGrpcClient(
    const std::string& host,
    const std::string& port,
    rpc::ClientPolicy policy,
    std::size_t pool_size)
    : _pool(std::make_unique<RPCConnection>(
          pool_size,
          host,
          port,
          policy.acquire_timeout)),
      _policy(policy) {}

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid) {
    GetChatServerReq request;
    request.set_uid(uid);
    auto result = rpc::InvokeUnary<RPCConnection, GetChatServerReq, GetChatServerRsp>(
        *_pool,
        request,
        _policy.rpc_deadline,
        [](StatusService::Stub& stub,
           ClientContext& context,
           const GetChatServerReq& req,
           GetChatServerRsp& rsp) {
            return stub.GetChatServer(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}
