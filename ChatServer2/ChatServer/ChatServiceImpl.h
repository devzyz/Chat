#pragma once
#include "grpcpp/grpcpp.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
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

using grpc::ClientContext;

class ChatServiceImpl final : public ChatService::Service
{
public:
	ChatServiceImpl();
	virtual Status NotifyOtherAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response) override;
	virtual Status NotifyOtherAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response) override;
	virtual Status NotifyOtherReceiveTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response) override;
	bool GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& user_info);
private:

};

