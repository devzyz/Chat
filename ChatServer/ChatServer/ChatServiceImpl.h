#pragma once
#include "grpcpp/grpcpp.h"
#include "chat.grpc.pb.h"
#include <memory>
#include <atomic>
#include "DependencyExecutor.h"
class CSession;

using grpc::Server;
using grpc::Status;
using grpc::ServerContext;
using grpc::ServerBuilder;

using message::ChatService;
using message::AddFriendReq;
using message::AddFriendRsp;

using message::AuthFriendReq;
using message::AuthFriendRsp;

using message::TextChatMsgReq;
using message::TextChatMsgRsp;

using message::KickUserReq;
using message::KickUserRsp;

using grpc::ClientContext;

class UserSessionDirectory;
class UserPresenceStore;
class SessionLifecycleCoordinator;
/** @brief 接收跨 ChatServer 的好友、消息、回执和替换登录通知并路由到本地会话。 */
class ChatServiceImpl final : public ChatService::Service {
public:
    /** @brief 接收跨实例回执变化并提示本地目标会话补拉；提示不代替持久化回执事实。 */
    Status NotifyMessageReceiptChanged(
        ServerContext* context,
        const message::ReceiptChangedReq* request,
        message::ReceiptChangedRsp* response) override;
    /** @brief 初始化ChatServiceImpl，保存构造参数及依赖引用。 */
    ChatServiceImpl(
        std::shared_ptr<UserSessionDirectory> directory,
        std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
        std::shared_ptr<UserPresenceStore> presence,
        std::shared_ptr<DependencyExecutor> dependencies);
    // 立即拒绝新入站投递，已有调用在受控期限内退出。
    void StopAdmission() {
        _stopping.store(true);
    }
    /** @brief 接收跨实例好友申请，向本实例接收方在线会话推送通知。 */
    virtual Status NotifyOtherAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response)
        override;
    /** @brief 接收跨实例好友审批及会话资料，向本实例目标会话推送。 */
    virtual Status NotifyOtherAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response)
        override;
    /** @brief 接收已提交文本消息并推送到本实例目标会话；不据此推断用户已读。 */
    virtual Status NotifyOtherReceiveTextChatMsg(
        ServerContext* context,
        const TextChatMsgReq* request,
        TextChatMsgRsp* response) override;
    /** @brief 接收替换登录通知，按请求中的会话身份关闭本实例对应旧会话。 */
    virtual Status NotifyOtherKickUser(ServerContext* context, const KickUserReq* request, KickUserRsp* reponse)
        override;

private:
    // 入站线程等待 Redis 池的鉴权结果，超时或取消禁止通过迟到回调发送。
    Status AuthorizeRecipient(ServerContext* context, int uid, std::shared_ptr<CSession>& session);
    // 发送前检查 RPC 是否仍有效。
    bool CanDeliver(ServerContext* context) const;
    std::atomic<bool> _stopping{false};
    std::shared_ptr<DependencyExecutor> _dependencies;
    std::shared_ptr<UserSessionDirectory> _directory;
    std::shared_ptr<SessionLifecycleCoordinator> _lifecycle;
    std::shared_ptr<UserPresenceStore> _presence;
};
