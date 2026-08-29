#pragma once

#include "../../common/grpc/GrpcClientRuntime.h"
#include "Singleton.h"
#include "const.h"
#include "varify.grpc.pb.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

using grpc::ClientContext;
using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::VarifyService;

class RPCConnectionPool : public rpc::BoundedPool<VarifyService::Stub> {
public:
    RPCConnectionPool(
        std::size_t pool_size,
        const std::string& host,
        const std::string& port,
        std::chrono::milliseconds acquire_timeout)
        : rpc::BoundedPool<VarifyService::Stub>(
              pool_size,
              acquire_timeout,
              [endpoint = host + ":" + port] {
                  return VarifyService::NewStub(grpc::CreateChannel(
                      endpoint,
                      grpc::InsecureChannelCredentials()));
              }) {}
};

class VerifyGrpcClient : public Singleton<VerifyGrpcClient> {
    friend class Singleton<VerifyGrpcClient>;

public:
    GetVarifyRsp GetVarifyCode(std::string email);
    VerifyGrpcClient(
        const std::string& host,
        const std::string& port,
        rpc::ClientPolicy policy,
        std::size_t pool_size = 5);

private:
    VerifyGrpcClient();

    std::unique_ptr<RPCConnectionPool> _pool;
    rpc::ClientPolicy _policy;
};
