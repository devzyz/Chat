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
    Status NotifyMessageReceiptChanged(ServerContext* context, const message::ReceiptChangedReq* request,
        message::ReceiptChangedRsp* response) override;
	ChatServiceImpl(std::shared_ptr<UserSessionDirectory> directory,
        std::shared_ptr<SessionLifecycleCoordinator> lifecycle);
	virtual Status NotifyOtherAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response) override;
	virtual Status NotifyOtherAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response) override;
	virtual Status NotifyOtherReceiveTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response) override;
	virtual Status NotifyOtherKickUser(ServerContext* context, const KickUserReq* request, KickUserRsp* reponse) override;
	/** @brief 优先读取 Redis，未命中时回源 MySQL 并回填；查不到用户返回 false。 */
	bool GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& user_info);
private:
	std::shared_ptr<UserSessionDirectory> _directory;
    std::shared_ptr<SessionLifecycleCoordinator> _lifecycle;
};
