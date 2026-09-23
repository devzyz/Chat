#include "ChatGrpcClient.h"

message::ReceiptChangedRsp ChatGrpcClient::NotifyMessageReceiptChanged(
    const std::string& server_name, const message::ReceiptChangedReq& request) {
    auto found = _pool.find(server_name);
    if (found == _pool.end()) {
        message::ReceiptChangedRsp response;
        response.set_error(ErrorCodes::RPCFailed);
        return response;
    }
    auto result = rpc::InvokeUnary<ChatConnectionPool, message::ReceiptChangedReq, message::ReceiptChangedRsp>(
        *found->second, request, _policy.rpc_deadline,
        /** @brief 在既定截止时间内转发回执变化 RPC。 */ [](ChatService::Stub& stub, ClientContext& context, const message::ReceiptChangedReq& req,
           message::ReceiptChangedRsp& rsp) { return stub.NotifyMessageReceiptChanged(&context, req, &rsp); });
    if (!result) result.response.set_error(ErrorCodes::RPCFailed);
    return result.response;
}

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
        /** @brief 按节及键读取对端路由配置。 */ [&config](const std::string& section, const std::string& key) {
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
        /** @brief 执行好友申请通知 RPC。 */ [](ChatService::Stub& stub,
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
        /** @brief 执行好友认证通知 RPC。 */ [](ChatService::Stub& stub,
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
        /** @brief 执行文本消息通知 RPC。 */ [](ChatService::Stub& stub,
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
        /** @brief 执行旧登录踢出通知 RPC。 */ [](ChatService::Stub& stub,
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
