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

/** @brief 保存 RPC 连接或 stub 所需的共享运行对象。 */
class RPCConnection : public rpc::BoundedPool<StatusService::Stub> {
public:
    /** @brief 初始化RPCConnection，保存 RPC 连接或 stub 所需的共享运行对象。 */
    RPCConnection(
        std::size_t pool_size,
        const std::string& host,
        const std::string& port,
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
    /** @brief 请求 Status 选择聊天服务器并颁发登录信息，返回协议响应及失败映射。 */
    GetChatServerRsp GetChatServer(int uid);
    /** @brief 按显式端点、策略和容量建立 Status stub 池，不发起业务 RPC。 */
    StatusGrpcClient(
        const std::string& host,
        const std::string& port,
        rpc::ClientPolicy policy,
        std::size_t pool_size = 5);

private:
    /** @brief 读取本服务 Status 端点与 RPC 策略后建立 stub 池。 */
    StatusGrpcClient();

    std::unique_ptr<RPCConnection> _pool;
    rpc::ClientPolicy _policy;
};
