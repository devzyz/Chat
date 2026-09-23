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
/** 短暂绑定并释放随机回环端口，提供预期不可用的测试端点。 */
unsigned short FindUnavailableLoopbackPort() {
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor acceptor(
        context,
        { boost::asio::ip::address_v4::loopback(), 0 });
    const auto port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

/** 提供确定性的 Status 登录 RPC 测试服务。 */
class ControlledStatusService final : public message::StatusService::Service {
public:
    /** 回显请求 UID 并返回登录成功。 */
    grpc::Status Login(
        grpc::ServerContext*,
        const message::LoginReq* request,
        message::LoginRsp* response) override {
        response->set_error(ErrorCodes::Success);
        response->set_uid(request->uid());
        return grpc::Status::OK;
    }
};

/** 提供可延时的 Chat RPC 测试服务以覆盖身份和期限。 */
class ControlledChatService final : public message::ChatService::Service {
public:
    /** 核对踢出请求的用户与旧会话标识后返回对应业务结果。 */
    grpc::Status NotifyOtherKickUser(grpc::ServerContext*, const message::KickUserReq* request,
        message::KickUserRsp* response) override {
        response->set_error(request->uid() == 42 && request->session_id() == "old-session"
            ? ErrorCodes::Success : ErrorCodes::UidInvalid);
        response->set_uid(request->uid());
        return grpc::Status::OK;
    }
    /** 按故障开关延时后回显申请者并返回成功。 */
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

/** 拥有在随机回环端口共享监听的 Status 和 Chat 服务。 */
class ChatLoopbackServer {
public:
    /** 注册测试服务并启动回环监听，绑定失败抛异常。 */
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

    /** 停止所属回环 gRPC 服务。 */
    ~ChatLoopbackServer() { Shutdown(); }

    /** 以两秒截止时间关闭服务并释放实例。 */
    void Shutdown() {
        if (_server) {
            _server->Shutdown(std::chrono::system_clock::now() + 2s);
            _server.reset();
        }
    }

    /** 返回实际绑定端口字符串。 */
    std::string Port() const { return std::to_string(_port); }
    /** 借用可控 Chat 服务，对象存活期间可设置故障开关。 */
    ControlledChatService& Chat() { return _chat; }

private:
    int _port = 0;
    ControlledStatusService _status;
    ControlledChatService _chat;
    std::unique_ptr<grpc::Server> _server;
};

/** 为单个测试对端创建指定策略与单容量的生产 Chat 客户端。 */
ChatGrpcClient MakeChatClient(const std::string& port, rpc::ClientPolicy policy) {
    ChatGrpcClient::EndpointMap endpoints{
        { "peer", { "127.0.0.1", port } }
    };
    return ChatGrpcClient(std::move(endpoints), policy, 1);
}

/** 构造固定申请双方身份的好友请求。 */
message::AddFriendReq AddFriendRequest() {
    message::AddFriendReq request;
    request.set_applyuid(42);
    request.set_touid(84);
    return request;
}
}

/** 验证生产 Status 与 Chat 客户端调用真实回环服务并保留会话身份。 */
TEST(ChatGrpcClientIntegrationTests, StatusAndChatClientsCallDynamicLoopbackServices) {
    ChatLoopbackServer server;
    const rpc::ClientPolicy policy{ 50ms, 200ms };
    StatusGrpcClient status("127.0.0.1", server.Port(), policy, 1);
    auto chat = MakeChatClient(server.Port(), policy);

    EXPECT_EQ(status.Login(42, "synthetic-token").error(), ErrorCodes::Success);
    EXPECT_EQ(chat.NotifyOtherAddFriend("peer", AddFriendRequest()).error(), ErrorCodes::Success);
    message::KickUserReq kick;
    kick.set_uid(42);
    kick.set_session_id("old-session");
    EXPECT_EQ(chat.NotifyOtherKickUser("peer", kick).error(), ErrorCodes::Success);
}

/** 验证对端延时按配置期限映射为 RPCFailed。 */
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

/** 验证不可用对端有界返回 RPCFailed。 */
TEST(ChatGrpcClientIntegrationTests, UnavailablePeerReturnsRpcFailedWithinDeadline) {
    auto client = MakeChatClient(
        std::to_string(FindUnavailableLoopbackPort()),
        { 50ms, 150ms });

    const auto response = client.NotifyOtherAddFriend("peer", AddFriendRequest());

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
}

/** 验证已关闭对端有界返回 RPCFailed。 */
TEST(ChatGrpcClientIntegrationTests, PeerShutdownMapsToRpcFailedWithinDeadline) {
    ChatLoopbackServer server;
    auto client = MakeChatClient(server.Port(), { 50ms, 150ms });
    server.Shutdown();

    const auto response = client.NotifyOtherAddFriend("peer", AddFriendRequest());

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
}
