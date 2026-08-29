#pragma once

#include "../../common/grpc/GrpcClientRuntime.h"
#include "Const.h"
#include "chat.grpc.pb.h"
#include "singleton.h"

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

class ChatConnectionPool : public rpc::BoundedPool<ChatService::Stub> {
public:
    ChatConnectionPool(
        const std::string& host,
        const std::string& port,
        std::size_t pool_size,
        std::chrono::milliseconds acquire_timeout)
        : rpc::BoundedPool<ChatService::Stub>(
              pool_size,
              acquire_timeout,
              [endpoint = host + ":" + port] {
                  return ChatService::NewStub(grpc::CreateChannel(
                      endpoint,
                      grpc::InsecureChannelCredentials()));
              }) {}
};

class ChatGrpcClient : public Singleton<ChatGrpcClient> {
    friend class Singleton<ChatGrpcClient>;

public:
    using EndpointMap = std::unordered_map<std::string, std::pair<std::string, std::string>>;

    ~ChatGrpcClient() = default;
    ChatGrpcClient(EndpointMap endpoints, rpc::ClientPolicy policy, std::size_t pool_size = 5);

    AddFriendRsp NotifyOtherAddFriend(const std::string& server_name, const AddFriendReq& request);
    AuthFriendRsp NotifyOtherAuthFriend(const std::string& server_name, const AuthFriendReq& request);
    TextChatMsgRsp NotifyOtherReceiveTextChatMsg(const std::string& server_name, const TextChatMsgReq& request);
    KickUserRsp NotifyOtherKickUser(const std::string& server_name, const KickUserReq& request);

private:
    ChatGrpcClient();

    std::unordered_map<std::string, std::unique_ptr<ChatConnectionPool>> _pool;
    rpc::ClientPolicy _policy;
};
