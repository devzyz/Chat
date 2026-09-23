#pragma once
#include "grpcpp/grpcpp.h"
#include "chat.grpc.pb.h"
#include "Data.h"
#include <memory>

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
class SessionLifecycleCoordinator;
/** @brief 接收跨 ChatServer 的好友、消息、回执和替换登录通知并路由到本地会话。 */
class ChatServiceImpl final : public ChatService::Service
{
public:
    /** @brief 接收跨实例回执变化并提示本地目标会话补拉；提示不代替持久化回执事实。 */
    Status NotifyMessageReceiptChanged(ServerContext* context, const message::ReceiptChangedReq* request,
        message::ReceiptChangedRsp* response) override;
	/** @brief 初始化ChatServiceImpl，保存构造参数及依赖引用。 */
	ChatServiceImpl(std::shared_ptr<UserSessionDirectory> directory,
        std::shared_ptr<SessionLifecycleCoordinator> lifecycle);
	/** @brief 接收跨实例好友申请，向本实例接收方在线会话推送通知。 */
	virtual Status NotifyOtherAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response) override;
	/** @brief 接收跨实例好友审批及会话资料，向本实例目标会话推送。 */
	virtual Status NotifyOtherAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response) override;
	/** @brief 接收已提交文本消息并推送到本实例目标会话；不据此推断用户已读。 */
	virtual Status NotifyOtherReceiveTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response) override;
	/** @brief 接收替换登录通知，按请求中的会话身份关闭本实例对应旧会话。 */
	virtual Status NotifyOtherKickUser(ServerContext* context, const KickUserReq* request, KickUserRsp* reponse) override;
	/** @brief 优先读取 Redis，未命中时回源 MySQL 并回填；查不到用户返回 false。 */
	bool GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& user_info);
private:
	std::shared_ptr<UserSessionDirectory> _directory;
    std::shared_ptr<SessionLifecycleCoordinator> _lifecycle;
};
