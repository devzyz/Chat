#pragma once

#include "../../common/grpc/GrpcClientRuntime.h"
#include "Const.h"
#include "chat.grpc.pb.h"
#include "Singleton.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

using grpc::ClientContext;
using message::AddFriendReq;
using message::AddFriendRsp;
using message::AuthFriendReq;
using message::AuthFriendRsp;
using message::ChatService;
using message::KickUserReq;
using message::KickUserRsp;
using message::TextChatMsgReq;
using message::TextChatMsgRsp;

/** @brief 按单个对端维护有界 gRPC stub 池，借用期限和调用期限分别约束。 */
class ChatConnectionPool : public rpc::BoundedPool<ChatService::Stub> {
public:
    /** @brief 初始化ChatConnectionPool，按单个对端维护有界 gRPC stub 池，借用期限和调用期限分别约束。 */
    ChatConnectionPool(
        const std::string& host,
        const std::string& port,
        std::size_t pool_size,
        std::chrono::milliseconds acquire_timeout)
        : rpc::BoundedPool<ChatService::Stub>(
              pool_size,
              acquire_timeout,
              /** @brief 为固定对端创建 ChatService stub，通道由 stub 共享持有。 */ [endpoint = host + ":" + port] {
                  return ChatService::NewStub(grpc::CreateChannel(
                      endpoint,
                      grpc::InsecureChannelCredentials()));
              }) {}
};

/** @brief 持有对端 ChatServer 的 RPC 池并发送跨实例业务通知，不把 RPC 成功等同于用户已读。 */
class ChatGrpcClient : public Singleton<ChatGrpcClient> {
    friend class Singleton<ChatGrpcClient>;

public:
    using EndpointMap = std::unordered_map<std::string, std::pair<std::string, std::string>>;

    /** @brief 销毁各对端 stub 池；调用方须先结束所有并发 RPC 与借用。 */
    ~ChatGrpcClient() = default;
    /** @brief 按显式对端表、策略和容量建立各实例 stub 池，不发送业务通知。 */
    ChatGrpcClient(EndpointMap endpoints, rpc::ClientPolicy policy, std::size_t pool_size = 5);

    /** @brief 向好友所在实例转发新的好友申请通知。 */
    AddFriendRsp NotifyOtherAddFriend(const std::string& server_name, const AddFriendReq& request);
    /** @brief 向对端实例转发好友审批及会话资料。 */
    AuthFriendRsp NotifyOtherAuthFriend(const std::string& server_name, const AuthFriendReq& request);
    /** @brief 向对端实例通知已提交文本消息；不据此推断用户已读。 */
    TextChatMsgRsp NotifyOtherReceiveTextChatMsg(const std::string& server_name, const TextChatMsgReq& request);
    /** @brief 请求对端实例使指定旧登录会话下线。 */
    KickUserRsp NotifyOtherKickUser(const std::string& server_name, const KickUserReq& request);
    /** @brief 向对端实例通知回执发生变化；接收方仍需按 revision 同步持久化事实。 */
    message::ReceiptChangedRsp NotifyMessageReceiptChanged(const std::string& server_name,
        const message::ReceiptChangedReq& request);

private:
    /** @brief 读取本实例配置中的对端列表与 RPC 策略，建立各实例 stub 池。 */
    ChatGrpcClient();

    std::unordered_map<std::string, std::unique_ptr<ChatConnectionPool>> _pool;
    rpc::ClientPolicy _policy;
};
