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

/** 提供可延时的验证码 RPC 对端，供生产客户端期限测试。 */
class ControlledVarifyService final : public message::VarifyService::Service {
public:
    /** 按故障开关延时后回显邮箱并返回成功。 */
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

/** 提供固定聊天端点的 Status 选服测试服务。 */
class ControlledStatusService final : public message::StatusService::Service {
public:
    /** 返回固定测试聊天地址与端口。 */
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

/** 拥有同一随机回环监听器上的验证码与 Status 服务。 */
class GateLoopbackServer {
public:
    /** 注册测试服务并启动监听，失败抛异常。 */
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

    /** 停止所属回环 gRPC 服务。 */
    ~GateLoopbackServer() { Shutdown(); }

    /** 以两秒截止时间关闭服务并释放实例。 */
    void Shutdown() {
        if (_server) {
            _server->Shutdown(std::chrono::system_clock::now() + 2s);
            _server.reset();
        }
    }

    /** 返回实际绑定端口字符串。 */
    std::string Port() const { return std::to_string(_port); }
    /** 借用验证码服务以控制测试延迟。 */
    ControlledVarifyService& Varify() { return _varify; }

private:
    int _port = 0;
    ControlledVarifyService _varify;
    ControlledStatusService _status;
    std::unique_ptr<grpc::Server> _server;
};
}

/** 验证生产验证码与 Status 客户端可调用真实动态回环服务。 */
TEST(GateGrpcClientIntegrationTests, VarifyAndStatusClientsCallDynamicLoopbackServices) {
    GateLoopbackServer server;
    const rpc::ClientPolicy policy{ 50ms, 200ms };
    VerifyGrpcClient varify("127.0.0.1", server.Port(), policy, 1);
    StatusGrpcClient status("127.0.0.1", server.Port(), policy, 1);

    EXPECT_EQ(varify.GetVarifyCode("bounded@example.test").error(), ErrorCodes::Success);
    EXPECT_EQ(status.GetChatServer(42).error(), ErrorCodes::Success);
}

/** 验证验证码 RPC 延时在配置期限内映射为 RPCFailed。 */
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

/** 验证不可用验证码端点有界返回 RPCFailed。 */
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

/** 验证对端关闭后有界返回 RPCFailed。 */
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
