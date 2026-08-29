#pragma once

#include "../../common/grpc/GrpcClientRuntime.h"
#include "Const.h"
#include "Singleton.h"
#include "status.grpc.pb.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

using grpc::ClientContext;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

class StatusConnectionPool : public rpc::BoundedPool<StatusService::Stub> {
public:
    StatusConnectionPool(
        const std::string& host,
        const std::string& port,
        std::size_t pool_size,
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
    LoginRsp Login(int uid, std::string token);
    StatusGrpcClient(
        const std::string& host,
        const std::string& port,
        rpc::ClientPolicy policy,
        std::size_t pool_size = 5);

private:
    StatusGrpcClient();

    std::unique_ptr<StatusConnectionPool> _pool;
    rpc::ClientPolicy _policy;
};
