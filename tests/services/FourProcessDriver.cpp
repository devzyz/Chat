#include "ProcessHarness.h"
#include "ChatFrameCodec.h"
#include "FrameFaultRelay.h"

#include <boost/asio.hpp>
#include <json/json.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace std::chrono_literals;
volatile std::sig_atomic_t stopping = 0;
/** 以信号安全的标志通知主流程停止。 */
void StopSignal(int) { stopping = 1; }
/** 条件不满足时抛出统一合同错误，避免泄漏协议敏感内容。 */
void Require(bool condition) { if (!condition) throw std::runtime_error("four_process_contract"); }
/** 解析 JSON 输入，格式错误时中止合同。 */
Json::Value Parse(const std::string& input) {
    Json::Value value;
    Json::Reader reader;
    Require(reader.parse(input, value));
    return value;
}

// Each request uses the production codec and a hard socket deadline. No test
// parser or private handler invocation bypasses the formal Chat transport.
/** 拥有一条 Chat TCP 连接，为四进程合同执行有界协议交互。 */
class WireClient {
public:
    /** 在有界异步操作中连接指定 loopback 端口。 */
    explicit WireClient(unsigned short port) : socket_(io_), timer_(io_) {
        Run(/** 启动 TCP 连接并把结果交给统一完成回调。 */ [&](auto done) { socket_.async_connect(
            {boost::asio::ip::make_address("127.0.0.1"), port}, done); });
    }
    /** 编码并发送完整请求；普通模式要求响应帧，拒绝模式仅接受零响应字节后的明确断开。 */
    Json::Value Request(int id, const Json::Value& body, bool expect_disconnect = false) {
        const auto text = Json::writeString(Json::StreamWriterBuilder{}, body);
        Require(text.size() <= MAX_LENGTH);
        const auto header = ChatFrameCodec::EncodeHeader(id, static_cast<std::uint16_t>(text.size()));
        const auto bytes = std::string(reinterpret_cast<const char*>(header.data()), header.size()) + text;
        Run(/** 异步写入完整请求字节，保持缓冲有效直到操作完成。 */ [&](auto done) { boost::asio::async_write(socket_, boost::asio::buffer(bytes),
            /** 将写入错误交给统一截止管理。 */ [done](auto error, auto) { done(error); }); });
        ChatFrameCodec::HeaderBytes response{};
        std::size_t header_bytes = 0;
        const auto header_error = Run(/** 异步读取响应帧头，保留字节数以拒绝截断帧。 */ [&](auto done) {
            boost::asio::async_read(socket_, boost::asio::buffer(response),
                /** 保存实际字节数并将读取结果交给统一截止管理。 */ [done, &header_bytes](auto error, auto bytes) {
                    header_bytes = bytes; done(error);
                });
        }, expect_disconnect);
        if (expect_disconnect) {
            Require(header_bytes == 0 && (header_error == boost::asio::error::eof ||
                header_error == boost::asio::error::connection_reset));
            Json::Value rejected;
            rejected["disconnected"] = true;
            return rejected;
        }
        const auto decoded = ChatFrameCodec::DecodeValidatedHeader(response.data(), MAX_LENGTH);
        Require(decoded && decoded->message_id == id + 1);
        std::string result(decoded->body_length, '\0');
        Run(/** 异步读取帧头声明的完整载荷。 */ [&](auto done) { boost::asio::async_read(socket_, boost::asio::buffer(result),
            /** 将载荷读取结果交给统一截止管理。 */ [done](auto error, auto) { done(error); }); });
        return Parse(result);
    }
private:
    /** 为单次异步动作提供五秒期限；观察模式返回错误供调用方严格分类，默认失败即中止。 */
    template<class Action> boost::system::error_code Run(Action action, bool observe_error = false) {
        io_.restart();
        boost::system::error_code result = boost::asio::error::timed_out;
        timer_.expires_after(5s);
        timer_.async_wait(/** 期限到达时关闭连接以结束未完成操作。 */ [&](auto error) { if (!error) { boost::system::error_code ignored; socket_.close(ignored); } });
        action(/** 保存动作结果并取消超时计时器。 */ [&](auto error) { result = error; timer_.cancel(); });
        io_.run();
        if (!observe_error) Require(!result);
        return result;
    }
    boost::asio::io_context io_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer timer_;
};

/** 从环境读取协议计划，登录后执行允许的请求并输出响应证据。 */
int Chat() {
    const auto* raw = std::getenv("CHAT_FOUR_WIRE");
    Require(raw != nullptr);
    const auto input = Parse(raw);
    const int port = input["port"].asInt();
    Require(port > 0 && port <= 65535);
    WireClient client(static_cast<unsigned short>(port));
    const auto login = client.Request(MSG_CHAT_LOGIN_REQ, input["login"]);
    Require(login["error"].isInt());
    Json::Value output;
    output["error"] = login["error"];
    output["responses"] = Json::Value(Json::arrayValue);
    if (login["error"].asInt() == 0) {
        for (const auto& request : input["requests"]) {
            const int id = request["id"].asInt();
            Require(id == MSG_CREATE_PRIVATE_CHAT_REQ || id == MSG_TEXT_CHAT_MSG_REQ || id == MSG_LOAD_CHAT_MESSAGE_REQ);
            const bool expect_disconnect = request.get("expect_disconnect", false).asBool();
            output["responses"].append(client.Request(id, request["body"], expect_disconnect));
            if (expect_disconnect) break;
        }
    }
    // Login tokens and private account data never leave the helper.
    std::cout << Json::writeString(Json::StreamWriterBuilder{}, output) << '\n';
    return 0;
}

/** 用运行上下文监督指定子进程，处理停止信号并收集清理证据。 */
int Supervise(int argc, char** argv) {
    Require(argc >= 5);
    std::signal(SIGTERM, StopSignal);
    std::signal(SIGINT, StopSignal);
    auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 600s);
    integration::ProcessSpec spec;
    spec.executable = std::filesystem::absolute(argv[2]);
    Require(std::filesystem::is_directory(std::filesystem::absolute(argv[3])));
    spec.working_directory = context->TempRoot();
    for (int index = 5; index < argc; ++index) spec.arguments.push_back(std::filesystem::path(argv[index]).wstring());
    auto child = integration::ProcessHarness::Start(*context, std::move(spec));
    {
        const auto identity = child->Identity();
        Json::Value value;
        value["pid"] = identity.pid;
        value["creationTime"] = std::to_string(identity.creation_time);
        std::ofstream output(std::string(argv[4]) + ".identity");
        output << Json::writeString(Json::StreamWriterBuilder{}, value);
        Require(static_cast<bool>(output));
    }
    const bool completed = child->WaitReady(/** 在收到停止信号或子进程退出时结束监督等待。 */ [&] { return stopping || child->CollectEvidence().exit_code.has_value(); },
        context->Deadline() - 15s);
    const auto cleanup = child->Stop(std::chrono::steady_clock::now() + 10s);
    const auto evidence = child->CollectEvidence();
    const auto outcome = context->Teardown();
    Json::Value report;
    report["complete"] = completed && cleanup.Complete() && outcome.complete && !evidence.escalated;
    report["escalated"] = evidence.escalated;
    report["exitCode"] = evidence.exit_code ? Json::Value(*evidence.exit_code) : Json::Value();
    std::ofstream output(argv[4]);
    output << Json::writeString(Json::StreamWriterBuilder{}, report);
    output.close();
    return output && report["complete"].asBool() ? 0 : 1;
}
}

/** 分派协议客户端、监督器或故障中继模式，异常时以失败退出。 */
int main(int argc, char** argv) {
    try {
        Require(argc >= 2);
        if (std::string(argv[1]) == "relay") {
            Require(argc == 3);
            const auto* raw = std::getenv("CHAT_RELAY_CONFIG");
            Require(raw != nullptr);
            const auto config = Parse(raw);
            const int port = config["port"].asInt(), backend = config["backend"].asInt();
            Require(port > 0 && port <= 65535 && backend > 0 && backend <= 65535 && port != backend);
            Require(config["dropUuid"].asString().size() == 36 && config["replayUuid"].asString().size() == 36);
            Require(std::filesystem::path(argv[2]).is_absolute());
            std::signal(SIGTERM, StopSignal);
            std::signal(SIGINT, StopSignal);
            FrameFaultRelay relay(static_cast<unsigned short>(port), static_cast<unsigned short>(backend),
                config["dropUuid"].asString(), config["replayUuid"].asString(), argv[2]);
            return relay.Run(/** 向中继报告是否已收到停止信号。 */ [] { return stopping != 0; });
        }
        if (std::string(argv[1]) == "chat") return Chat();
        if (std::string(argv[1]) == "supervise") return Supervise(argc, argv);
        if (std::string(argv[1]) == "validation") return 0;
    } catch (const std::exception&) { std::cerr << "four_process_driver_failed\n"; }
    return 1;
}
