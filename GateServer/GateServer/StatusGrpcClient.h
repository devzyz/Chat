#pragma once

#include "../../common/grpc/GrpcClientRuntime.h"
#include "Singleton.h"
#include "const.h"
#include "status.grpc.pb.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

using grpc::ClientContext;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;

class RPCConnection : public rpc::BoundedPool<StatusService::Stub> {
public:
    RPCConnection(
        std::size_t pool_size,
        const std::string& host,
        const std::string& port,
        std::chrono::milliseconds acquire_timeout)
        : rpc::BoundedPool<StatusService::Stub>(
              pool_size,
              acquire_timeout,
              [endpoint = host + ":" + port] {
                  return StatusService::NewStub(grpc::CreateChannel(
                      endpoint,
                      grpc::InsecureChannelCredentials()));
              }) {}
};

class StatusGrpcClient : public Singleton<StatusGrpcClient> {
    friend class Singleton<StatusGrpcClient>;

public:
    ~StatusGrpcClient() = default;
    GetChatServerRsp GetChatServer(int uid);
    StatusGrpcClient(
        const std::string& host,
        const std::string& port,
        rpc::ClientPolicy policy,
        std::size_t pool_size = 5);

private:
    StatusGrpcClient();

    std::unique_ptr<RPCConnection> _pool;
    rpc::ClientPolicy _policy;
};
