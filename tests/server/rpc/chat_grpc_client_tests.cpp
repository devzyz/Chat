#include <gtest/gtest.h>

#include "ChatGrpcClient.h"
#include "StatusGrpcClient.h"

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {
unsigned short FindUnavailableLoopbackPort() {
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor acceptor(
        context,
        { boost::asio::ip::address_v4::loopback(), 0 });
    const auto port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

class ControlledStatusService final : public message::StatusService::Service {
public:
    grpc::Status Login(
        grpc::ServerContext*,
        const message::LoginReq* request,
        message::LoginRsp* response) override {
        response->set_error(ErrorCodes::Success);
        response->set_uid(request->uid());
        return grpc::Status::OK;
    }
};

class ControlledChatService final : public message::ChatService::Service {
public:
    grpc::Status NotifyOtherAddFriend(
        grpc::ServerContext*,
        const message::AddFriendReq* request,
        message::AddFriendRsp* response) override {
        if (delay.load()) {
            std::this_thread::sleep_for(400ms);
        }
        response->set_error(ErrorCodes::Success);
        response->set_applyuid(request->applyuid());
        return grpc::Status::OK;
    }

    std::atomic<bool> delay{ false };
};

class ChatLoopbackServer {
public:
    ChatLoopbackServer() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &_port);
        builder.RegisterService(&_status);
        builder.RegisterService(&_chat);
        _server = builder.BuildAndStart();
        if (!_server || _port == 0) {
            throw std::runtime_error("failed to start Chat gRPC loopback fixture");
        }
    }

    ~ChatLoopbackServer() { Shutdown(); }

    void Shutdown() {
        if (_server) {
            _server->Shutdown(std::chrono::system_clock::now() + 2s);
            _server.reset();
        }
    }

    std::string Port() const { return std::to_string(_port); }
    ControlledChatService& Chat() { return _chat; }

private:
    int _port = 0;
    ControlledStatusService _status;
    ControlledChatService _chat;
    std::unique_ptr<grpc::Server> _server;
};

ChatGrpcClient MakeChatClient(const std::string& port, rpc::ClientPolicy policy) {
    ChatGrpcClient::EndpointMap endpoints{
        { "peer", { "127.0.0.1", port } }
    };
    return ChatGrpcClient(std::move(endpoints), policy, 1);
}

message::AddFriendReq AddFriendRequest() {
    message::AddFriendReq request;
    request.set_applyuid(42);
    request.set_touid(84);
    return request;
}
}

TEST(ChatGrpcClientIntegrationTests, StatusAndChatClientsCallDynamicLoopbackServices) {
    ChatLoopbackServer server;
    const rpc::ClientPolicy policy{ 50ms, 200ms };
    StatusGrpcClient status("127.0.0.1", server.Port(), policy, 1);
    auto chat = MakeChatClient(server.Port(), policy);

    EXPECT_EQ(status.Login(42, "synthetic-token").error(), ErrorCodes::Success);
    EXPECT_EQ(chat.NotifyOtherAddFriend("peer", AddFriendRequest()).error(), ErrorCodes::Success);
}

TEST(ChatGrpcClientIntegrationTests, DeadlineExceededMapsToRpcFailedWithinConfiguredDeadline) {
    ChatLoopbackServer server;
    server.Chat().delay = true;
    auto client = MakeChatClient(server.Port(), { 50ms, 100ms });

    const auto started = std::chrono::steady_clock::now();
    const auto response = client.NotifyOtherAddFriend("peer", AddFriendRequest());
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
    EXPECT_GE(elapsed, 75ms);
    EXPECT_LT(elapsed, 1s);
}

TEST(ChatGrpcClientIntegrationTests, UnavailablePeerReturnsRpcFailedWithinDeadline) {
    auto client = MakeChatClient(
        std::to_string(FindUnavailableLoopbackPort()),
        { 50ms, 150ms });

    const auto response = client.NotifyOtherAddFriend("peer", AddFriendRequest());

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
}

TEST(ChatGrpcClientIntegrationTests, PeerShutdownMapsToRpcFailedWithinDeadline) {
    ChatLoopbackServer server;
    auto client = MakeChatClient(server.Port(), { 50ms, 150ms });
    server.Shutdown();

    const auto response = client.NotifyOtherAddFriend("peer", AddFriendRequest());

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
}
