#include "ProcessHarness.h"
#include "ChatFrameCodec.h"

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
void StopSignal(int) { stopping = 1; }
void Require(bool condition) { if (!condition) throw std::runtime_error("four_process_contract"); }
Json::Value Parse(const std::string& input) {
    Json::Value value;
    Json::Reader reader;
    Require(reader.parse(input, value));
    return value;
}

// Each request uses the production codec and a hard socket deadline. No test
// parser or private handler invocation bypasses the formal Chat transport.
class WireClient {
public:
    explicit WireClient(unsigned short port) : socket_(io_), timer_(io_) {
        Run([&](auto done) { socket_.async_connect(
            {boost::asio::ip::make_address("127.0.0.1"), port}, done); });
    }
    Json::Value Request(int id, const Json::Value& body) {
        const auto text = Json::writeString(Json::StreamWriterBuilder{}, body);
        Require(text.size() <= MAX_LENGTH);
        const auto header = ChatFrameCodec::EncodeHeader(id, static_cast<std::uint16_t>(text.size()));
        const auto bytes = std::string(reinterpret_cast<const char*>(header.data()), header.size()) + text;
        Run([&](auto done) { boost::asio::async_write(socket_, boost::asio::buffer(bytes),
            [done](auto error, auto) { done(error); }); });
        ChatFrameCodec::HeaderBytes response{};
        Run([&](auto done) { boost::asio::async_read(socket_, boost::asio::buffer(response),
            [done](auto error, auto) { done(error); }); });
        const auto decoded = ChatFrameCodec::DecodeValidatedHeader(response.data(), MAX_LENGTH);
        Require(decoded && decoded->message_id == id + 1);
        std::string result(decoded->body_length, '\0');
        Run([&](auto done) { boost::asio::async_read(socket_, boost::asio::buffer(result),
            [done](auto error, auto) { done(error); }); });
        return Parse(result);
    }
private:
    template<class Action> void Run(Action action) {
        io_.restart();
        boost::system::error_code result = boost::asio::error::timed_out;
        timer_.expires_after(5s);
        timer_.async_wait([&](auto error) { if (!error) { boost::system::error_code ignored; socket_.close(ignored); } });
        action([&](auto error) { result = error; timer_.cancel(); });
        io_.run();
        Require(!result);
    }
    boost::asio::io_context io_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer timer_;
};

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
            output["responses"].append(client.Request(id, request["body"]));
        }
    }
    // Login tokens and private account data never leave the helper.
    std::cout << Json::writeString(Json::StreamWriterBuilder{}, output) << '\n';
    return 0;
}

int Supervise(int argc, char** argv) {
    Require(argc >= 5);
    std::signal(SIGTERM, StopSignal);
    std::signal(SIGINT, StopSignal);
    auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 240s);
    integration::ProcessSpec spec;
    spec.executable = std::filesystem::absolute(argv[2]);
    Require(std::filesystem::is_directory(std::filesystem::absolute(argv[3])));
    spec.working_directory = context->TempRoot();
    for (int index = 5; index < argc; ++index) spec.arguments.push_back(std::filesystem::path(argv[index]).wstring());
    auto child = integration::ProcessHarness::Start(*context, std::move(spec));
    const bool completed = child->WaitReady([&] { return stopping || child->CollectEvidence().exit_code.has_value(); },
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

int main(int argc, char** argv) {
    try {
        Require(argc >= 2);
        if (std::string(argv[1]) == "chat") return Chat();
        if (std::string(argv[1]) == "supervise") return Supervise(argc, argv);
        if (std::string(argv[1]) == "validation") return 0;
    } catch (const std::exception&) { std::cerr << "four_process_driver_failed\n"; }
    return 1;
}
