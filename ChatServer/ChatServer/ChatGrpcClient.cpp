#include "ChatGrpcClient.h"

#include "ConfigMgr.h"
#include "PeerServerRouting.h"

ChatGrpcClient::ChatGrpcClient() {
    auto& config = ConfigMgr::GetInstance();
    auto grpc_config = config["Grpc"];
    _policy = {
        rpc::ParseDurationMs(
            grpc_config["PoolAcquireTimeoutMs"],
            "[Grpc].PoolAcquireTimeoutMs",
            std::chrono::milliseconds(1000)),
        rpc::ParseDurationMs(
            grpc_config["ChatDeadlineMs"],
            "[Grpc].ChatDeadlineMs",
            std::chrono::milliseconds(3000))
    };

    const auto endpoints = ResolvePeerServerEndpoints(
        config["PeerServer"]["Servers"],
        [&config](const std::string& section, const std::string& key) {
            return config[section][key];
        });
    for (const auto& endpoint : endpoints) {
        _pool[endpoint.first] = std::make_unique<ChatConnectionPool>(
            endpoint.second.host,
            endpoint.second.port,
            5,
            _policy.acquire_timeout);
    }
}

ChatGrpcClient::ChatGrpcClient(
    EndpointMap endpoints,
    rpc::ClientPolicy policy,
    std::size_t pool_size)
    : _policy(policy) {
    for (const auto& endpoint : endpoints) {
        _pool[endpoint.first] = std::make_unique<ChatConnectionPool>(
            endpoint.second.first,
            endpoint.second.second,
            pool_size,
            _policy.acquire_timeout);
    }
}

AddFriendRsp ChatGrpcClient::NotifyOtherAddFriend(
    const std::string& server_name,
    const AddFriendReq& request) {
    auto found = _pool.find(server_name);
    if (found == _pool.end()) {
        AddFriendRsp response;
        response.set_error(ErrorCodes::RPCFailed);
        return response;
    }
    auto result = rpc::InvokeUnary<ChatConnectionPool, AddFriendReq, AddFriendRsp>(
        *found->second,
        request,
        _policy.rpc_deadline,
        [](ChatService::Stub& stub,
           ClientContext& context,
           const AddFriendReq& req,
           AddFriendRsp& rsp) {
            return stub.NotifyOtherAddFriend(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}

AuthFriendRsp ChatGrpcClient::NotifyOtherAuthFriend(
    const std::string& server_name,
    const AuthFriendReq& request) {
    auto found = _pool.find(server_name);
    if (found == _pool.end()) {
        AuthFriendRsp response;
        response.set_error(ErrorCodes::RPCFailed);
        return response;
    }
    auto result = rpc::InvokeUnary<ChatConnectionPool, AuthFriendReq, AuthFriendRsp>(
        *found->second,
        request,
        _policy.rpc_deadline,
        [](ChatService::Stub& stub,
           ClientContext& context,
           const AuthFriendReq& req,
           AuthFriendRsp& rsp) {
            return stub.NotifyOtherAuthFriend(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}

TextChatMsgRsp ChatGrpcClient::NotifyOtherReceiveTextChatMsg(
    const std::string& server_name,
    const TextChatMsgReq& request) {
    auto found = _pool.find(server_name);
    if (found == _pool.end()) {
        TextChatMsgRsp response;
        response.set_error(ErrorCodes::RPCFailed);
        return response;
    }
    auto result = rpc::InvokeUnary<ChatConnectionPool, TextChatMsgReq, TextChatMsgRsp>(
        *found->second,
        request,
        _policy.rpc_deadline,
        [](ChatService::Stub& stub,
           ClientContext& context,
           const TextChatMsgReq& req,
           TextChatMsgRsp& rsp) {
            return stub.NotifyOtherReceiveTextChatMsg(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}

KickUserRsp ChatGrpcClient::NotifyOtherKickUser(
    const std::string& server_name,
    const KickUserReq& request) {
    auto found = _pool.find(server_name);
    if (found == _pool.end()) {
        KickUserRsp response;
        response.set_error(ErrorCodes::RPCFailed);
        return response;
    }
    auto result = rpc::InvokeUnary<ChatConnectionPool, KickUserReq, KickUserRsp>(
        *found->second,
        request,
        _policy.rpc_deadline,
        [](ChatService::Stub& stub,
           ClientContext& context,
           const KickUserReq& req,
           KickUserRsp& rsp) {
            return stub.NotifyOtherKickUser(&context, req, &rsp);
        });
    if (!result) {
        result.response.set_error(ErrorCodes::RPCFailed);
    }
    return result.response;
}
