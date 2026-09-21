#include "../../support/ProcessHarness.h"
#include "../../support/RunContext.h"

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>
#include <Windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

enum class FormalService { Gate, Status, Chat };

const char* ServiceName(FormalService service) {
    switch (service) {
    case FormalService::Gate: return "GateServer";
    case FormalService::Status: return "StatusServer";
    case FormalService::Chat: return "ChatServer";
    }
    return "UnknownServer";
}

class ScopedEnvironmentValue {
public:
    ScopedEnvironmentValue(const wchar_t* name, const std::optional<std::wstring>& value) : name_(name) {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required > 0) {
            std::wstring current(required, L'\0');
            GetEnvironmentVariableW(name, current.data(), required);
            current.resize(required - 1);
            original_ = std::move(current);
        }
        if (!SetEnvironmentVariableW(name, value ? value->c_str() : nullptr)) {
            throw std::runtime_error("unable to set scoped environment value");
        }
    }

    ~ScopedEnvironmentValue() {
        SetEnvironmentVariableW(name_.c_str(), original_ ? original_->c_str() : nullptr);
    }

private:
    std::wstring name_;
    std::optional<std::wstring> original_;
};

std::filesystem::path RepositoryRoot() {
    std::vector<wchar_t> module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        throw std::runtime_error("unable to resolve test executable path");
    }
    module_path.resize(length);
    const std::filesystem::path test_executable(module_path.data());
    return test_executable.parent_path().parent_path().parent_path().parent_path();
}

std::filesystem::path FindExecutable(FormalService service) {
    std::vector<wchar_t> module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        throw std::runtime_error("unable to resolve test executable path");
    }
    module_path.resize(length);
    const auto configuration = std::filesystem::path(module_path.data()).parent_path().filename();
    const auto name = ServiceName(service);
    return RepositoryRoot() / "build" / "windows-servers" / configuration / name /
        (std::string(name) + ".exe");
}

void WriteText(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) {
        throw std::runtime_error("unable to write owned fixture " + path.string());
    }
}

void WriteGateStatusConfig(
    FormalService service,
    const std::filesystem::path& path,
    std::uint16_t port,
    const std::string& run_identity) {
    std::ostringstream config;
    config << "[GateServer]\nPort = " << (service == FormalService::Gate ? port : 1)
           << "\n[VarifyServer]\nHost = 127.0.0.1\nPort = 1"
           << "\n[Redis]\nHost = 127.0.0.1\nPort = 1\nPassword = " << run_identity
           << "\n[Mysql]\nHost = 127.0.0.1\nPort = 1\nPassword = " << run_identity
           << "\nUser = local\nSchema = local"
           << "\n[StatusServer]\nHost = 127.0.0.1\nPort = "
           << (service == FormalService::Status ? port : 1)
           << "\n[ChatServers]\nName = LocalChat"
           << "\n[LocalChat]\nHost = 127.0.0.1\nPort = 1\nName = LocalChat"
           << "\n[Log]\nName = " << ServiceName(service)
           << "Composition\nLogDir = logs\nMaxSizeMB = 1\nMaxTotalFiles = 2"
           << "\nLevel = info\nFlushLevel = warn\n";
    WriteText(path, config.str());
}

void WriteChatConfig(
    const std::filesystem::path& path,
    std::uint16_t tcp_port,
    std::uint16_t grpc_port,
    const std::string& run_identity) {
    std::ostringstream config;
    config << "[SelfServer]\nName = CompositionChat\nHost = 127.0.0.1"
           << "\nPort = " << tcp_port << "\nRPCPort = " << grpc_port
           << "\n[Redis]\nHost = 127.0.0.1\nPort = 1\nPassword = " << run_identity
           << "\n[Mysql]\nHost = 127.0.0.1\nPort = 1\nPassword = " << run_identity
           << "\nUser = local\nSchema = local"
           << "\n[StatusServer]\nHost = 127.0.0.1\nPort = 1"
           << "\n[PeerServer]\nServers ="
           << "\n[Log]\nName = ChatComposition\nLogDir = logs\nMaxSizeMB = 1"
           << "\nMaxTotalFiles = 2\nLevel = info\nFlushLevel = warn\n";
    WriteText(path, config.str());
}

std::unique_ptr<integration::ProcessHarness> StartFormal(
    integration::RunContext& context,
    FormalService service,
    const std::filesystem::path& config,
    const std::filesystem::path& working_directory) {
    const auto executable = FindExecutable(service);
    if (!std::filesystem::is_regular_file(executable)) {
        throw std::runtime_error("missing formal executable " + executable.string());
    }
    integration::ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = {L"--config", config.wstring()};
    spec.working_directory = working_directory;
    return integration::ProcessHarness::Start(context, std::move(spec));
}

bool ProbeGate(std::uint16_t port) {
    boost::asio::io_context context;
    boost::asio::ip::tcp::socket socket(context);
    boost::system::error_code error;
    socket.connect({boost::asio::ip::address_v4::loopback(), port}, error);
    if (error) return false;
    const std::string request =
        "GET /get_test HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    boost::asio::write(socket, boost::asio::buffer(request), error);
    if (error) return false;
    socket.non_blocking(true, error);
    std::array<char, 1024> buffer{};
    std::string response;
    const auto deadline = std::chrono::steady_clock::now() + 300ms;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto count = socket.read_some(boost::asio::buffer(buffer), error);
        if (!error) {
            response.append(buffer.data(), count);
            if (response.find("200 OK") != std::string::npos &&
                response.find("receive get_test req") != std::string::npos) {
                return true;
            }
        } else if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) {
            error.clear();
            std::this_thread::yield();
        } else {
            return false;
        }
    }
    return false;
}

bool ProbeStatus(std::uint16_t port) {
    auto channel = grpc::CreateChannel(
        "127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials());
    return channel->WaitForConnected(std::chrono::system_clock::now() + 300ms);
}

void ExpectPortReleased(std::uint16_t port, bool wildcard) {
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor acceptor(context);
    boost::system::error_code error;
    acceptor.open(boost::asio::ip::tcp::v4(), error);
    ASSERT_FALSE(error) << error.message();
    const BOOL exclusive = TRUE;
    ASSERT_NE(setsockopt(
        acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)), SOCKET_ERROR);
    acceptor.bind(
        {wildcard ? boost::asio::ip::address_v4::any() : boost::asio::ip::address_v4::loopback(), port},
        error);
    EXPECT_FALSE(error) << "formal process retained port " << port << ": " << error.message();
}

void ExpectSelectedInvalidConfig(FormalService service) {
    auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 10s);
    const auto working = context->CreateOwnedDirectory("formal-config");
    const auto selected = working / "explicit-selected.ini";
    const auto ignored = working / "environment-ignored.ini";
    WriteText(selected, "not an ini document\n");
    WriteText(ignored, "also not an ini document\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", ignored.wstring());
    auto process = StartFormal(*context, service, selected, working);

    EXPECT_FALSE(process->WaitReady([] { return false; }, std::chrono::steady_clock::now() + 5s));
    const auto stop = process->Stop(std::chrono::steady_clock::now() + 2s);
    const auto evidence = process->CollectEvidence();
    EXPECT_TRUE(stop.Complete()) << stop.Detail();
    ASSERT_TRUE(evidence.exit_code.has_value());
    EXPECT_NE(*evidence.exit_code, 0U);
    EXPECT_FALSE(evidence.graceful_stop_attempted);
    EXPECT_NE(evidence.stderr_text.find("explicit-selected.ini"), std::string::npos)
        << evidence.stderr_text;
    EXPECT_EQ(evidence.stderr_text.find("environment-ignored.ini"), std::string::npos)
        << evidence.stderr_text;
    process.reset();
    EXPECT_TRUE(context->Teardown().complete);
}

void ExpectReadyAndGracefulStop(FormalService service) {
    auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 20s);
    const auto working = context->CreateOwnedDirectory("formal-ready");
    const auto reservation = context->ReserveLoopbackPort("formal-listener");
    ASSERT_TRUE(context->ReleaseLoopbackPort("formal-listener"));
    const auto config = working / "ready.ini";
    WriteGateStatusConfig(service, config, reservation.port, context->SyntheticIdentity("credential"));
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    auto process = StartFormal(*context, service, config, working);

    const auto ready = process->WaitReady(
        [service, port = reservation.port] {
            return service == FormalService::Gate ? ProbeGate(port) : ProbeStatus(port);
        },
        std::chrono::steady_clock::now() + 10s);
    ASSERT_TRUE(ready) << process->CollectEvidence().stderr_text;
    const auto stop = process->Stop(std::chrono::steady_clock::now() + 5s);
    const auto evidence = process->CollectEvidence();
    EXPECT_TRUE(stop.Complete()) << stop.Detail();
    EXPECT_TRUE(evidence.ready_probe_succeeded);
    EXPECT_TRUE(evidence.graceful_stop_attempted);
    EXPECT_FALSE(evidence.escalated);
    EXPECT_TRUE(evidence.pipe_readers_closed);
    ASSERT_TRUE(evidence.exit_code.has_value());
    EXPECT_EQ(*evidence.exit_code, 0U) << evidence.stderr_text;
    process.reset();
    ExpectPortReleased(reservation.port, service == FormalService::Gate);
    EXPECT_TRUE(context->Teardown().complete);
}

struct RefusalEvidence {
    std::optional<std::uint32_t> exit_code;
    bool graceful_stop_attempted = false;
    bool escalated = false;
    bool ready_probe_succeeded = false;
    bool pipe_readers_closed = false;
    std::string stdout_text;
    std::string stderr_text;
};

RefusalEvidence ObserveChatRealDependencyBoundary(
    integration::RunContext& context,
    const std::filesystem::path& working,
    const std::filesystem::path& config) {
    auto process = StartFormal(context, FormalService::Chat, config, working);
    EXPECT_FALSE(process->WaitReady([] { return false; }, std::chrono::steady_clock::now() + 7s));
    const auto stop = process->Stop(std::chrono::steady_clock::now() + 2s);
    const auto evidence = process->CollectEvidence();
    EXPECT_TRUE(stop.Complete()) << stop.Detail();
    RefusalEvidence result{
        evidence.exit_code,
        evidence.graceful_stop_attempted,
        evidence.escalated,
        evidence.ready_probe_succeeded,
        evidence.pipe_readers_closed,
        evidence.stdout_text,
        evidence.stderr_text};
    process.reset();
    return result;
}
} // namespace

// T09-COMP-01
TEST(T09_COMP_Formal, GateExplicitConfigWinsAndInvalidSelectionFailsClosed) {
    ExpectSelectedInvalidConfig(FormalService::Gate);
}

// T09-COMP-02
TEST(T09_COMP_Formal, GateProtocolReadyGracefulSignalAndPortReleaseAreBounded) {
    ExpectReadyAndGracefulStop(FormalService::Gate);
}

// T09-COMP-03
TEST(T09_COMP_Formal, StatusExplicitConfigWinsAndInvalidSelectionFailsClosed) {
    ExpectSelectedInvalidConfig(FormalService::Status);
}

// T09-COMP-04
TEST(T09_COMP_Formal, StatusProtocolReadyGracefulSignalAndPortReleaseAreBounded) {
    ExpectReadyAndGracefulStop(FormalService::Status);
}

// T09-COMP-05
TEST(T09_COMP_Formal, ChatExplicitConfigWinsAndInvalidSelectionFailsClosed) {
    ExpectSelectedInvalidConfig(FormalService::Chat);
}

// T09-COMP-06
TEST(T09_COMP_Formal, ChatRealDependencyBoundaryIsExplicitBoundedAndResidueFree) {
    auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 25s);
    const auto working = context->CreateOwnedDirectory("chat-refusal");
    const auto tcp = context->ReserveLoopbackPort("chat-tcp");
    const auto grpc = context->ReserveLoopbackPort("chat-grpc");
    ASSERT_TRUE(context->ReleaseLoopbackPort("chat-tcp"));
    ASSERT_TRUE(context->ReleaseLoopbackPort("chat-grpc"));
    const auto config = working / "loopback-dependency-refusal.ini";
    WriteChatConfig(config, tcp.port, grpc.port, context->SyntheticIdentity("credential"));
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);

    // ChatServer cannot complete its formal ready path without the disposable Redis/MySQL
    // services owned by Phase 3C. Keep that boundary explicit: observe that no 3B ready
    // probe succeeds, then prove that the real process can still be stopped and cleaned up
    // within the owned deadline. Do not add a fake-dependency mode to make this path green.
    const auto first = ObserveChatRealDependencyBoundary(*context, working, config);
    const auto second = ObserveChatRealDependencyBoundary(*context, working, config);
    ASSERT_TRUE(first.exit_code.has_value());
    ASSERT_TRUE(second.exit_code.has_value());
    EXPECT_NE(*first.exit_code, 0U);
    EXPECT_EQ(*first.exit_code, *second.exit_code);
    EXPECT_FALSE(first.ready_probe_succeeded);
    EXPECT_FALSE(second.ready_probe_succeeded);
    EXPECT_TRUE(first.graceful_stop_attempted);
    EXPECT_TRUE(second.graceful_stop_attempted);
    EXPECT_EQ(first.escalated, second.escalated);
    EXPECT_TRUE(first.pipe_readers_closed);
    EXPECT_TRUE(second.pipe_readers_closed);
    for (const auto* text : {
             &first.stdout_text, &first.stderr_text, &second.stdout_text, &second.stderr_text}) {
        EXPECT_EQ(text->find("http://"), std::string::npos) << *text;
        EXPECT_EQ(text->find("https://"), std::string::npos) << *text;
        EXPECT_EQ(text->find(context->SyntheticIdentity("credential")), std::string::npos) << *text;
    }
    ExpectPortReleased(tcp.port, false);
    ExpectPortReleased(grpc.port, false);
    EXPECT_TRUE(context->Teardown().complete);
}
