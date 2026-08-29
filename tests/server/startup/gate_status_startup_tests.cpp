#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include <Windows.h>

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

constexpr DWORD STARTUP_TIMEOUT_MS = 15000;
constexpr DWORD STOP_TIMEOUT_MS = 5000;

enum class ServiceKind {
    Gate,
    Status,
};

const char* ServiceName(ServiceKind service) {
    return service == ServiceKind::Gate ? "GateServer" : "StatusServer";
}

class TempDirectory {
public:
    TempDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto suffix = std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(attempt);
            _path = base / ("gate-status-startup-tests-" + suffix);
            std::error_code error;
            if (std::filesystem::create_directory(_path, error)) {
                return;
            }
        }
        throw std::runtime_error("unable to create a unique Gate/Status startup test directory");
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(_path, error);
        if (error) {
            ADD_FAILURE() << "failed to remove owned temp directory " << _path.string()
                << ": " << error.message();
        }
    }

    const std::filesystem::path& Path() const {
        return _path;
    }

private:
    std::filesystem::path _path;
};

class ScopedEnvironmentValue {
public:
    ScopedEnvironmentValue(const wchar_t* name, const std::optional<std::wstring>& value) : _name(name) {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required > 0) {
            std::wstring current(required, L'\0');
            GetEnvironmentVariableW(name, current.data(), required);
            current.resize(required - 1);
            _original = current;
        }
        if (!SetEnvironmentVariableW(name, value ? value->c_str() : nullptr)) {
            throw std::runtime_error("unable to set the scoped test environment variable");
        }
    }

    ~ScopedEnvironmentValue() {
        SetEnvironmentVariableW(_name.c_str(), _original ? _original->c_str() : nullptr);
    }

private:
    std::wstring _name;
    std::optional<std::wstring> _original;
};

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

class OwnedProcess {
public:
    OwnedProcess(
        const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& output_prefix)
        : _executable(std::filesystem::absolute(executable)),
          _stdout_path(working_directory / (output_prefix + ".stdout.txt")),
          _stderr_path(working_directory / (output_prefix + ".stderr.txt")) {
        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;

        HANDLE stdout_handle = CreateFileW(
            _stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE stderr_handle = CreateFileW(
            _stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stdout_handle == INVALID_HANDLE_VALUE || stderr_handle == INVALID_HANDLE_VALUE) {
            if (stdout_handle != INVALID_HANDLE_VALUE) CloseHandle(stdout_handle);
            if (stderr_handle != INVALID_HANDLE_VALUE) CloseHandle(stderr_handle);
            throw std::runtime_error("unable to create child output files");
        }

        std::wstring command_line = Quote(_executable.wstring());
        for (const auto& argument : arguments) {
            command_line += L" " + Quote(argument);
        }
        std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back(L'\0');

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup_info.hStdOutput = stdout_handle;
        startup_info.hStdError = stderr_handle;

        const BOOL created = CreateProcessW(
            _executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NEW_PROCESS_GROUP, nullptr, working_directory.c_str(),
            &startup_info, &_process_info);
        CloseHandle(stdout_handle);
        CloseHandle(stderr_handle);
        if (!created) {
            throw std::runtime_error("CreateProcessW failed with error " + std::to_string(GetLastError()));
        }
    }

    OwnedProcess(const OwnedProcess&) = delete;
    OwnedProcess& operator=(const OwnedProcess&) = delete;

    ~OwnedProcess() {
        if (IsRunning()) {
            if (!IdentityMatches()) {
                ADD_FAILURE() << "refusing to terminate an unverified process identity, pid="
                    << _process_info.dwProcessId;
            } else if (!TerminateProcess(_process_info.hProcess, 98) ||
                       WaitForSingleObject(_process_info.hProcess, STOP_TIMEOUT_MS) != WAIT_OBJECT_0) {
                ADD_FAILURE() << "failed to terminate owned process, pid=" << _process_info.dwProcessId;
            }
        }
        if (_process_info.hThread) CloseHandle(_process_info.hThread);
        if (_process_info.hProcess) CloseHandle(_process_info.hProcess);
    }

    DWORD Pid() const {
        return _process_info.dwProcessId;
    }

    bool IsRunning() const {
        return WaitForSingleObject(_process_info.hProcess, 0) == WAIT_TIMEOUT;
    }

    bool WaitForExit(DWORD timeout_ms) const {
        return WaitForSingleObject(_process_info.hProcess, timeout_ms) == WAIT_OBJECT_0;
    }

    DWORD ExitCode() const {
        DWORD exit_code = STILL_ACTIVE;
        if (!GetExitCodeProcess(_process_info.hProcess, &exit_code)) {
            throw std::runtime_error("GetExitCodeProcess failed");
        }
        return exit_code;
    }

    bool SendCtrlBreak() const {
        return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, _process_info.dwProcessId) != FALSE;
    }

    bool ForceTerminate(DWORD exit_code) {
        if (!IsRunning() || !IdentityMatches()) {
            return false;
        }
        if (!TerminateProcess(_process_info.hProcess, exit_code)) {
            return false;
        }
        return WaitForExit(STOP_TIMEOUT_MS);
    }

    std::string Stdout() const { return ReadAll(_stdout_path); }
    std::string Stderr() const { return ReadAll(_stderr_path); }

private:
    bool IdentityMatches() const {
        if (GetProcessId(_process_info.hProcess) != _process_info.dwProcessId) {
            return false;
        }
        std::vector<wchar_t> image_path(32768, L'\0');
        DWORD image_length = static_cast<DWORD>(image_path.size());
        if (!QueryFullProcessImageNameW(_process_info.hProcess, 0, image_path.data(), &image_length)) {
            return false;
        }
        image_path.resize(image_length);
        const auto expected = std::filesystem::weakly_canonical(_executable).wstring();
        const auto actual = std::filesystem::weakly_canonical(image_path.data()).wstring();
        return CompareStringOrdinal(
            expected.c_str(), static_cast<int>(expected.size()),
            actual.c_str(), static_cast<int>(actual.size()), TRUE) == CSTR_EQUAL;
    }

    std::filesystem::path _executable;
    std::filesystem::path _stdout_path;
    std::filesystem::path _stderr_path;
    PROCESS_INFORMATION _process_info{};
};

std::filesystem::path RepositoryRoot() {
    std::vector<wchar_t> module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        throw std::runtime_error("unable to resolve the test executable path");
    }
    module_path.resize(length);
    const std::filesystem::path test_executable(module_path.data());
    return test_executable.parent_path().parent_path().parent_path().parent_path();
}

std::filesystem::path FindServiceExecutable(ServiceKind service) {
    std::vector<wchar_t> module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        throw std::runtime_error("unable to resolve the test executable path");
    }
    module_path.resize(length);
    const std::filesystem::path test_executable(module_path.data());
    const auto configuration = test_executable.parent_path().filename();
    const auto name = ServiceName(service);
    return RepositoryRoot() / "build" / "windows-servers" / configuration / name / (std::string(name) + ".exe");
}

void WriteText(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) {
        throw std::runtime_error("unable to write fixture " + path.string());
    }
}

unsigned short ReservePort(boost::asio::ip::tcp::acceptor& acceptor, bool wildcard = false) {
    acceptor.open(boost::asio::ip::tcp::v4());
    const BOOL exclusive = TRUE;
    if (setsockopt(
            acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
        throw std::runtime_error("unable to reserve an exclusive loopback port");
    }
    acceptor.bind({wildcard ? boost::asio::ip::address_v4::any() : boost::asio::ip::address_v4::loopback(), 0});
    acceptor.listen();
    return acceptor.local_endpoint().port();
}

unsigned short FindAvailablePort(bool wildcard = false) {
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor acceptor(context);
    const auto port = ReservePort(acceptor, wildcard);
    acceptor.close();
    return port;
}

void WriteServiceConfig(
    ServiceKind service,
    const std::filesystem::path& path,
    const std::string& service_port,
    bool include_critical_endpoint = true,
    const std::string& grpc_settings = {}) {
    std::ostringstream config;
    config << "[GateServer]\n"
           << "Port = " << (service == ServiceKind::Gate ? service_port : "1") << "\n"
           << "[VarifyServer]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "[Redis]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "Password = fixture-placeholder\n"
           << "[Mysql]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "Password = fixture-placeholder\n"
           << "User = fixture-user\n"
           << "Schema = fixture-schema\n"
           << "[StatusServer]\n";
    if (include_critical_endpoint) {
        config << "Host = 127.0.0.1\n";
    }
    config << "Port = " << (service == ServiceKind::Status ? service_port : "1") << "\n"
           << "[ChatServers]\n"
           << "Name = FixtureChat\n"
           << "[FixtureChat]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "Name = FixtureChat\n"
           << "[Log]\n"
           << "Name = " << ServiceName(service) << "StartupTest\n"
           << "LogDir = logs\n"
           << "MaxSizeMB = 1\n"
           << "MaxTotalFiles = 2\n"
           << "Level = info\n"
           << "FlushLevel = warn\n"
           << grpc_settings;
    WriteText(path, config.str());
}

bool ProbeGateHttp(unsigned short port) {
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
    if (error) return false;
    std::string response;
    std::array<char, 1024> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + 250ms;
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
            std::this_thread::sleep_for(10ms);
        } else {
            break;
        }
    }
    return false;
}

bool ProbeStatusGrpc(unsigned short port) {
    auto channel = grpc::CreateChannel(
        "127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials());
    return channel->WaitForConnected(std::chrono::system_clock::now() + 250ms);
}

bool WaitForReady(ServiceKind service, unsigned short port, const OwnedProcess& process) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(STARTUP_TIMEOUT_MS);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!process.IsRunning()) return false;
        if ((service == ServiceKind::Gate && ProbeGateHttp(port)) ||
            (service == ServiceKind::Status && ProbeStatusGrpc(port))) {
            return true;
        }
        std::this_thread::sleep_for(50ms);
    }
    return false;
}

struct CompletedProcess {
    DWORD exit_code = STILL_ACTIVE;
    bool timed_out = false;
    std::string stdout_text;
    std::string stderr_text;
};

CompletedProcess RunToExit(
    ServiceKind service,
    const std::vector<std::wstring>& arguments,
    const std::filesystem::path& working_directory) {
    const auto executable = FindServiceExecutable(service);
    if (!std::filesystem::is_regular_file(executable)) {
        throw std::runtime_error("missing production executable " + executable.string());
    }
    OwnedProcess process(executable, arguments, working_directory, ServiceName(service));
    CompletedProcess result;
    if (!process.WaitForExit(STARTUP_TIMEOUT_MS)) {
        result.timed_out = true;
        process.ForceTerminate(99);
    }
    result.exit_code = process.ExitCode();
    result.stdout_text = process.Stdout();
    result.stderr_text = process.Stderr();
    return result;
}

void ExpectFailure(const CompletedProcess& result) {
    EXPECT_FALSE(result.timed_out)
        << "stdout:\n" << result.stdout_text << "\nstderr:\n" << result.stderr_text;
    EXPECT_NE(result.exit_code, 0U)
        << "stdout:\n" << result.stdout_text << "\nstderr:\n" << result.stderr_text;
}

class GateStatusStartupTests : public testing::TestWithParam<ServiceKind> {};

std::string ServiceParameterName(const testing::TestParamInfo<ServiceKind>& info) {
    return ServiceName(info.param);
}
}

// S02-CLI-01 / S03-CLI-01
TEST_P(GateStatusStartupTests, MissingConfigValueReturnsNonZeroWithUsage) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto result = RunToExit(GetParam(), {L"--config"}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("Usage: " + std::string(ServiceName(GetParam())) + ".exe [--config <path>]"),
        std::string::npos) << result.stderr_text;
}

// S02-CLI-02 / S03-CLI-02
TEST_P(GateStatusStartupTests, UnknownArgumentReturnsNonZeroWithUsage) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto result = RunToExit(GetParam(), {L"--unknown"}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("Usage: " + std::string(ServiceName(GetParam())) + ".exe [--config <path>]"),
        std::string::npos) << result.stderr_text;
}

// S02-CFG-01 / S03-CFG-01
TEST_P(GateStatusStartupTests, MissingConfigFileReturnsNonZero) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto missing = fixture.Path() / "missing-selected.ini";
    const auto result = RunToExit(GetParam(), {L"--config", missing.wstring()}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("missing-selected.ini"), std::string::npos) << result.stderr_text;
}

// S02-CFG-02 / S03-CFG-02
TEST_P(GateStatusStartupTests, InvalidServicePortReturnsNonZero) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto config = fixture.Path() / "invalid-port.ini";
    WriteServiceConfig(GetParam(), config, "65536junk");
    const auto result = RunToExit(GetParam(), {L"--config", config.wstring()}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("Port"), std::string::npos) << result.stderr_text;
}

// S02-CFG-03 / S03-CFG-03
TEST_P(GateStatusStartupTests, MissingCriticalEndpointReturnsNonZero) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto config = fixture.Path() / "missing-endpoint.ini";
    WriteServiceConfig(GetParam(), config, std::to_string(FindAvailablePort()), false);
    const auto result = RunToExit(GetParam(), {L"--config", config.wstring()}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("[StatusServer].Host"), std::string::npos) << result.stderr_text;
}

// S02-CFG-04 / S03-CFG-04
TEST_P(GateStatusStartupTests, ExplicitConfigPathOverridesEnvironment) {
    TempDirectory fixture;
    const auto explicit_config = fixture.Path() / "explicit-selected.ini";
    const auto environment_config = fixture.Path() / "environment-not-selected.ini";
    WriteText(explicit_config, "not an ini document\n");
    WriteText(environment_config, "also not an ini document\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", environment_config.wstring());
    const auto result = RunToExit(GetParam(), {L"--config", explicit_config.wstring()}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("explicit-selected.ini"), std::string::npos) << result.stderr_text;
    EXPECT_EQ(result.stderr_text.find("environment-not-selected.ini"), std::string::npos) << result.stderr_text;
}

// S02-CFG-05 / S03-CFG-05
TEST_P(GateStatusStartupTests, EnvironmentConfigPathOverridesWorkingDirectoryDefault) {
    TempDirectory fixture;
    const auto environment_config = fixture.Path() / "environment-selected.ini";
    WriteText(environment_config, "not an ini document\n");
    WriteText(fixture.Path() / "config.ini", "default-not-selected\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", environment_config.wstring());
    const auto result = RunToExit(GetParam(), {}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("environment-selected.ini"), std::string::npos) << result.stderr_text;
}

// S02-CFG-06 / S03-CFG-06
TEST_P(GateStatusStartupTests, WorkingDirectoryConfigIsTheDefault) {
    TempDirectory fixture;
    WriteText(fixture.Path() / "config.ini", "default-selected-not-ini\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto result = RunToExit(GetParam(), {}, fixture.Path());
    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("config.ini"), std::string::npos) << result.stderr_text;
}

// S02-GRPC-CFG-01
TEST(GateGrpcStartupTests, OutOfRangeGrpcTimeoutReturnsNonZeroBeforeListening) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto config = fixture.Path() / "invalid-grpc-timeout.ini";
    WriteServiceConfig(
        ServiceKind::Gate,
        config,
        std::to_string(FindAvailablePort(true)),
        true,
        "[Grpc]\nPoolAcquireTimeoutMs = 99\n");

    const auto result = RunToExit(ServiceKind::Gate, {L"--config", config.wstring()}, fixture.Path());

    ExpectFailure(result);
    EXPECT_NE(result.stderr_text.find("PoolAcquireTimeoutMs"), std::string::npos) << result.stderr_text;
}

// S02-BIND-01 / S03-BIND-01
TEST_P(GateStatusStartupTests, OccupiedPortFailsWithoutProtocolReadyAndReleasesOwnership) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor holder(context);
    const bool wildcard = GetParam() == ServiceKind::Gate;
    const auto port = ReservePort(holder, wildcard);
    const auto config = fixture.Path() / "occupied-port.ini";
    WriteServiceConfig(GetParam(), config, std::to_string(port));

    const auto result = RunToExit(GetParam(), {L"--config", config.wstring()}, fixture.Path());
    ExpectFailure(result);
    EXPECT_FALSE(GetParam() == ServiceKind::Gate ? ProbeGateHttp(port) : ProbeStatusGrpc(port));

    holder.close();
    boost::asio::ip::tcp::acceptor released(context);
    boost::system::error_code error;
    released.open(boost::asio::ip::tcp::v4(), error);
    ASSERT_FALSE(error) << error.message();
    const BOOL exclusive = TRUE;
    ASSERT_NE(setsockopt(
        released.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)), SOCKET_ERROR);
    released.bind({wildcard ? boost::asio::ip::address_v4::any() : boost::asio::ip::address_v4::loopback(), port}, error);
    EXPECT_FALSE(error) << "port " << port << " was not released: " << error.message();
}

// S02-LIFE-01 / S03-LIFE-01
TEST_P(GateStatusStartupTests, ProtocolReadyThenCtrlBreakStopsWithinDeadlineAndReleasesPort) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const bool wildcard = GetParam() == ServiceKind::Gate;
    const auto port = FindAvailablePort(wildcard);
    const auto config = fixture.Path() / "ready-stop.ini";
    WriteServiceConfig(GetParam(), config, std::to_string(port));
    const auto executable = FindServiceExecutable(GetParam());
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();
    OwnedProcess process(executable, {L"--config", config.wstring()}, fixture.Path(), ServiceName(GetParam()));

    ASSERT_TRUE(WaitForReady(GetParam(), port, process))
        << "pid=" << process.Pid() << " stdout:\n" << process.Stdout()
        << "\nstderr:\n" << process.Stderr();
    ASSERT_TRUE(process.SendCtrlBreak()) << "GenerateConsoleCtrlEvent failed with " << GetLastError();
    ASSERT_TRUE(process.WaitForExit(STOP_TIMEOUT_MS))
        << "pid=" << process.Pid() << " stdout:\n" << process.Stdout()
        << "\nstderr:\n" << process.Stderr();
    EXPECT_EQ(process.ExitCode(), 0U)
        << "stdout:\n" << process.Stdout() << "\nstderr:\n" << process.Stderr();

    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor released(context);
    boost::system::error_code error;
    released.open(boost::asio::ip::tcp::v4(), error);
    ASSERT_FALSE(error) << error.message();
    const BOOL exclusive = TRUE;
    ASSERT_NE(setsockopt(
        released.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)), SOCKET_ERROR);
    released.bind({wildcard ? boost::asio::ip::address_v4::any() : boost::asio::ip::address_v4::loopback(), port}, error);
    EXPECT_FALSE(error) << "port " << port << " was not released: " << error.message();
}

INSTANTIATE_TEST_SUITE_P(
    ProductionExecutables,
    GateStatusStartupTests,
    testing::Values(ServiceKind::Gate, ServiceKind::Status),
    ServiceParameterName);
