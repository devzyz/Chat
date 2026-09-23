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

/** @brief 管理验证码 RPC stub 的有界租约，调用方不得让租约晚于池析构。 */
class RPCConnectionPool : public rpc::BoundedPool<VarifyService::Stub> {
public:
    /** @brief 初始化RPCConnectionPool，管理验证码 RPC stub 的有界租约，调用方不得让租约晚于池析构。 */
    RPCConnectionPool(
        std::size_t pool_size,
        const std::string& host,
        const std::string& port,
        std::chrono::milliseconds acquire_timeout)
        : rpc::BoundedPool<VarifyService::Stub>(
              pool_size,
              acquire_timeout,
              /** @brief 为固定端点构造验证码服务 stub。 */ [endpoint = host + ":" + port] {
                  return VarifyService::NewStub(grpc::CreateChannel(
                      endpoint,
                      grpc::InsecureChannelCredentials()));
              }) {}
};

/** @brief 调用外部 GetVarifyCode 协议，保留协议拼写并映射 RPC 失败。 */
class VerifyGrpcClient : public Singleton<VerifyGrpcClient> {
    friend class Singleton<VerifyGrpcClient>;

public:
    /** @brief 调用外部验证码 RPC 并返回其业务响应；保留协议方法名，不自动重试发信。 */
    GetVarifyRsp GetVarifyCode(std::string email);
    /** @brief 按显式端点、策略和容量建立验证码 stub 池，不触发发信。 */
    VerifyGrpcClient(
        const std::string& host,
        const std::string& port,
        rpc::ClientPolicy policy,
        std::size_t pool_size = 5);

private:
    /** @brief 读取本服务验证码端点与 RPC 策略后建立 stub 池。 */
    VerifyGrpcClient();

    std::unique_ptr<RPCConnectionPool> _pool;
    rpc::ClientPolicy _policy;
};
