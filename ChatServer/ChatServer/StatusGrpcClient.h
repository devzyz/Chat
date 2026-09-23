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

/** @brief 管理 Status RPC stub 的有界租约，关闭可唤醒等待借用者。 */
class StatusConnectionPool : public rpc::BoundedPool<StatusService::Stub> {
public:
    /** @brief 初始化StatusConnectionPool，管理 Status RPC stub 的有界租约，关闭可唤醒等待借用者。 */
    StatusConnectionPool(
        const std::string& host,
        const std::string& port,
        std::size_t pool_size,
        std::chrono::milliseconds acquire_timeout)
        : rpc::BoundedPool<StatusService::Stub>(
              pool_size,
              acquire_timeout,
              /** @brief 为固定端点构造 StatusService stub。 */ [endpoint = host + ":" + port] {
                  return StatusService::NewStub(grpc::CreateChannel(
                      endpoint,
                      grpc::InsecureChannelCredentials()));
              }) {}
};

/** @brief 调用 Status 选服或登录校验接口，按配置设置借用和 RPC 截止时间。 */
class StatusGrpcClient : public Singleton<StatusGrpcClient> {
    friend class Singleton<StatusGrpcClient>;

public:
    /** @brief 销毁持有的 stub 池；调用方须先结束所有并发 RPC 与借用。 */
    ~StatusGrpcClient() = default;
    /** @brief 向 Status 验证 UID 和 Token，按协议响应返回认证结果。 */
    LoginRsp Login(int uid, std::string token);
    /** @brief 按显式端点、策略和容量建立 Status stub 池，不发起业务 RPC。 */
    StatusGrpcClient(
        const std::string& host,
        const std::string& port,
        rpc::ClientPolicy policy,
        std::size_t pool_size = 5);

private:
    /** @brief 读取本服务 Status 端点与 RPC 策略后建立 stub 池。 */
    StatusGrpcClient();

    std::unique_ptr<StatusConnectionPool> _pool;
    rpc::ClientPolicy _policy;
};
