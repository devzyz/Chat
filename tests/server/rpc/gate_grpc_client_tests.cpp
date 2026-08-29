#include <gtest/gtest.h>

#include "../../../GateServer/GateServer/StatusGrpcClient.h"
#include "../../../GateServer/GateServer/VerifyGrpcClient.h"

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <memory>
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

class ControlledVarifyService final : public message::VarifyService::Service {
public:
    grpc::Status GetVarifyCode(
        grpc::ServerContext*,
        const message::GetVarifyReq* request,
        message::GetVarifyRsp* response) override {
        if (delay.load()) {
            std::this_thread::sleep_for(400ms);
        }
        response->set_error(ErrorCodes::Success);
        response->set_email(request->email());
        return grpc::Status::OK;
    }

    std::atomic<bool> delay{ false };
};

class ControlledStatusService final : public message::StatusService::Service {
public:
    grpc::Status GetChatServer(
        grpc::ServerContext*,
        const message::GetChatServerReq*,
        message::GetChatServerRsp* response) override {
        response->set_error(ErrorCodes::Success);
        response->set_host("127.0.0.1");
        response->set_port("9001");
        return grpc::Status::OK;
    }
};

class GateLoopbackServer {
public:
    GateLoopbackServer() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &_port);
        builder.RegisterService(&_varify);
        builder.RegisterService(&_status);
        _server = builder.BuildAndStart();
        if (!_server || _port == 0) {
            throw std::runtime_error("failed to start Gate gRPC loopback fixture");
        }
    }

    ~GateLoopbackServer() { Shutdown(); }

    void Shutdown() {
        if (_server) {
            _server->Shutdown(std::chrono::system_clock::now() + 2s);
            _server.reset();
        }
    }

    std::string Port() const { return std::to_string(_port); }
    ControlledVarifyService& Varify() { return _varify; }

private:
    int _port = 0;
    ControlledVarifyService _varify;
    ControlledStatusService _status;
    std::unique_ptr<grpc::Server> _server;
};
}

TEST(GateGrpcClientIntegrationTests, VarifyAndStatusClientsCallDynamicLoopbackServices) {
    GateLoopbackServer server;
    const rpc::ClientPolicy policy{ 50ms, 200ms };
    VerifyGrpcClient varify("127.0.0.1", server.Port(), policy, 1);
    StatusGrpcClient status("127.0.0.1", server.Port(), policy, 1);

    EXPECT_EQ(varify.GetVarifyCode("bounded@example.test").error(), ErrorCodes::Success);
    EXPECT_EQ(status.GetChatServer(42).error(), ErrorCodes::Success);
}

TEST(GateGrpcClientIntegrationTests, DeadlineExceededMapsToRpcFailedWithinConfiguredDeadline) {
    GateLoopbackServer server;
    server.Varify().delay = true;
    VerifyGrpcClient client(
        "127.0.0.1",
        server.Port(),
        rpc::ClientPolicy{ 50ms, 100ms },
        1);

    const auto started = std::chrono::steady_clock::now();
    const auto response = client.GetVarifyCode("deadline@example.test");
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
    EXPECT_GE(elapsed, 75ms);
    EXPECT_LT(elapsed, 1s);
}

TEST(GateGrpcClientIntegrationTests, UnavailableVarifyEndpointReturnsRpcFailedWithinDeadline) {
    const auto port = FindUnavailableLoopbackPort();
    VerifyGrpcClient client(
        "127.0.0.1",
        std::to_string(port),
        rpc::ClientPolicy{ 50ms, 150ms },
        1);

    const auto started = std::chrono::steady_clock::now();
    const auto response = client.GetVarifyCode("unavailable@example.test");
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
    EXPECT_LT(elapsed, 1s);
}

TEST(GateGrpcClientIntegrationTests, PeerShutdownMapsToRpcFailedWithinDeadline) {
    GateLoopbackServer server;
    VerifyGrpcClient client(
        "127.0.0.1",
        server.Port(),
        rpc::ClientPolicy{ 50ms, 150ms },
        1);
    server.Shutdown();

    const auto response = client.GetVarifyCode("shutdown@example.test");

    EXPECT_EQ(response.error(), ErrorCodes::RPCFailed);
}
