#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr DWORD PROCESS_TIMEOUT_MS = 10000;

/** 拥有启动测试的唯一临时目录，退出时清理所属文件。 */
class TempDirectory {
public:
    /** 有限次尝试创建唯一临时目录，失败抛异常。 */
    TempDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto suffix = std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(attempt);
            _path = base / ("chat-startup-tests-" + suffix);
            std::error_code error;
            if (std::filesystem::create_directory(_path, error)) {
                return;
            }
        }
        throw std::runtime_error("unable to create a unique startup test directory");
    }

    /** 清理本对象创建的目录，忽略析构期文件错误。 */
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(_path, error);
    }

    /** 借用所属临时目录路径。 */
    const std::filesystem::path& Path() const {
        return _path;
    }

private:
    std::filesystem::path _path;
};

/** 在作用域内设置或移除 Windows 环境变量并保存原状态。 */
class ScopedEnvironmentValue {
public:
    /** 读取原值后设置指定测试环境状态。 */
    ScopedEnvironmentValue(const wchar_t* name, const std::optional<std::wstring>& value) : _name(name) {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required > 0) {
            std::wstring current(required, L'\0');
            GetEnvironmentVariableW(name, current.data(), required);
            current.resize(required - 1);
            _original = current;
        }

        SetEnvironmentVariableW(name, value ? value->c_str() : nullptr);
    }

    /** 恢复原环境值或缺失状态。 */
    ~ScopedEnvironmentValue() {
        SetEnvironmentVariableW(_name.c_str(), _original ? _original->c_str() : nullptr);
    }

private:
    std::wstring _name;
    std::optional<std::wstring> _original;
};

/** 保存子进程退出码、期限状态及捕获输出。 */
struct ProcessResult {
    DWORD exit_code = 0;
    bool timed_out = false;
    std::string stdout_text;
    std::string stderr_text;
};

/** 按二进制读取输出文件的全部内容。 */
std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

/** 为受控测试命令参数添加双引号。 */
std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

/** 启动真实测试子进程并将输出定向到所属目录，有限等待后收集结果。 */
ProcessResult RunProcess(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::filesystem::path& working_directory) {
    const auto stdout_path = working_directory / "child.stdout.txt";
    const auto stderr_path = working_directory / "child.stderr.txt";

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_handle = CreateFileW(
        stdout_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security_attributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    HANDLE stderr_handle = CreateFileW(
        stderr_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security_attributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (stdout_handle == INVALID_HANDLE_VALUE || stderr_handle == INVALID_HANDLE_VALUE) {
        if (stdout_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdout_handle);
        }
        if (stderr_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(stderr_handle);
        }
        throw std::runtime_error("unable to create child output files");
    }

    std::wstring command_line = Quote(executable.wstring());
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

    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(
        executable.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_directory.c_str(),
        &startup_info,
        &process_info);
    CloseHandle(stdout_handle);
    CloseHandle(stderr_handle);
    if (!created) {
        throw std::runtime_error("CreateProcessW failed with error " + std::to_string(GetLastError()));
    }

    ProcessResult result;
    const DWORD wait_result = WaitForSingleObject(process_info.hProcess, PROCESS_TIMEOUT_MS);
    if (wait_result == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process_info.hProcess, 99);
        WaitForSingleObject(process_info.hProcess, PROCESS_TIMEOUT_MS);
    }
    if (!GetExitCodeProcess(process_info.hProcess, &result.exit_code)) {
        result.exit_code = STILL_ACTIVE;
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    result.stdout_text = ReadAll(stdout_path);
    result.stderr_text = ReadAll(stderr_path);
    return result;
}

/** 从测试可执行文件位置推导同配置的生产 ChatServer 路径。 */
std::filesystem::path FindChatServerExecutable() {
    std::vector<wchar_t> module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        throw std::runtime_error("unable to resolve the test executable path");
    }
    module_path.resize(length);

    const std::filesystem::path test_executable(module_path.data());
    const auto configuration = test_executable.parent_path().filename();
    const auto repository_root = test_executable.parent_path().parent_path().parent_path().parent_path();
    return repository_root / "build" / "windows-servers" / configuration / "ChatServer" / "ChatServer.exe";
}

/** 写入夹具文件，写失败时报告路径。 */
void WriteText(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) {
        throw std::runtime_error("unable to write fixture " + path.string());
    }
}

/** 独占绑定随机回环 TCP 端口并开始监听，返回实际端口。 */
unsigned short ReservePort(boost::asio::ip::tcp::acceptor& acceptor) {
    acceptor.open(boost::asio::ip::tcp::v4());
    const BOOL exclusive = TRUE;
    if (setsockopt(
            acceptor.native_handle(),
            SOL_SOCKET,
            SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive),
            sizeof(exclusive)) == SOCKET_ERROR) {
        throw std::runtime_error("unable to reserve an exclusive loopback port");
    }
    acceptor.bind({boost::asio::ip::address_v4::loopback(), 0});
    acceptor.listen();
    return acceptor.local_endpoint().port();
}

/** 独占绑定随机通配监听端口，用于复现生产绑定冲突。 */
unsigned short ReserveWildcardPort(boost::asio::ip::tcp::acceptor& acceptor) {
    acceptor.open(boost::asio::ip::tcp::v4());
    const BOOL exclusive = TRUE;
    if (setsockopt(
            acceptor.native_handle(),
            SOL_SOCKET,
            SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive),
            sizeof(exclusive)) == SOCKET_ERROR) {
        throw std::runtime_error("unable to reserve an exclusive wildcard port");
    }
    acceptor.bind({boost::asio::ip::address_v4::any(), 0});
    acceptor.listen();
    return acceptor.local_endpoint().port();
}

/** 临时独占端口后释放，提供待测服务可尝试使用的端口。 */
unsigned short FindAvailablePort() {
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor acceptor(context);
    const auto port = ReservePort(acceptor);
    acceptor.close();
    return port;
}

/** 生成指定 TCP、RPC 端口及可选 gRPC 设置的启动配置。 */
void WriteValidStartupConfig(
    const std::filesystem::path& path,
    unsigned short tcp_port,
    unsigned short grpc_port,
    const std::string& grpc_settings = {}) {
    std::ostringstream config;
    config << "[SelfServer]\n"
           << "Name = StartupTest\n"
           << "Host = 127.0.0.1\n"
           << "Port = " << tcp_port << "\n"
           << "RPCPort = " << grpc_port << "\n"
           << "[Redis]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "Password = test-placeholder\n"
           << "[Mysql]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "Password = test-placeholder\n"
           << "User = test-user\n"
           << "Schema = test-schema\n"
           << "[StatusServer]\n"
           << "Host = 127.0.0.1\n"
           << "Port = 1\n"
           << "[PeerServer]\n"
           << "Servers =\n"
           << "[Log]\n"
           << "Name = StartupTest\n"
           << "LogDir = logs\n"
           << "MaxSizeMB = 1\n"
           << "MaxTotalFiles = 2\n"
           << "Level = info\n"
           << "FlushLevel = warn\n"
           << grpc_settings;
    WriteText(path, config.str());
}

/** 断言启动在期限内失败并显示命令用法。 */
void ExpectPromptFailure(const ProcessResult& result) {
    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("Usage: ChatServer.exe [--config <path>]"), std::string::npos)
        << result.stderr_text;
}
}

// S01-CLI-01
/** 验证配置参数缺少路径值时非零退出并显示用法。 */
TEST(StartupConfigTests, MissingConfigValueReturnsNonZeroWithUsage) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {L"--config"}, fixture.Path());

    ExpectPromptFailure(result);
}

// S01-CLI-02
/** 验证未知命令行参数导致非零退出并显示用法。 */
TEST(StartupConfigTests, UnknownArgumentReturnsNonZeroWithUsage) {
    TempDirectory fixture;
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {L"--unknown"}, fixture.Path());

    ExpectPromptFailure(result);
}

// S01-CFG-01
/** 验证显式配置路径优先于环境指定路径。 */
TEST(StartupConfigTests, ExplicitConfigPathOverridesEnvironment) {
    TempDirectory fixture;
    const auto explicit_config = fixture.Path() / "explicit-selected.ini";
    const auto environment_config = fixture.Path() / "environment-not-selected.ini";
    WriteText(explicit_config, "this is not an ini file\n");
    WriteText(environment_config, "also not an ini file\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", environment_config.wstring());
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {L"--config", explicit_config.wstring()}, fixture.Path());

    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("explicit-selected.ini"), std::string::npos) << result.stderr_text;
    EXPECT_EQ(result.stderr_text.find("environment-not-selected.ini"), std::string::npos) << result.stderr_text;
}

// S01-CFG-02
/** 验证环境配置优先于工作目录默认配置。 */
TEST(StartupConfigTests, EnvironmentConfigPathOverridesWorkingDirectoryDefault) {
    TempDirectory fixture;
    const auto environment_config = fixture.Path() / "environment-selected.ini";
    WriteText(environment_config, "this is not an ini file\n");
    WriteText(fixture.Path() / "config.ini", "default should not be selected\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", environment_config.wstring());
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {}, fixture.Path());

    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("environment-selected.ini"), std::string::npos) << result.stderr_text;
}

// S01-CFG-03
/** 验证未指定路径时选择工作目录配置。 */
TEST(StartupConfigTests, WorkingDirectoryConfigIsTheDefault) {
    TempDirectory fixture;
    const auto default_config = fixture.Path() / "config.ini";
    WriteText(default_config, "this is not an ini file\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {}, fixture.Path());

    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("config.ini"), std::string::npos) << result.stderr_text;
}

// S01-GRPC-CFG-01
/** 验证越界 gRPC 期限在监听前拒绝启动。 */
TEST(StartupConfigTests, OutOfRangeGrpcTimeoutReturnsNonZeroBeforeListening) {
    TempDirectory fixture;
    const auto config_path = fixture.Path() / "invalid-grpc-timeout.ini";
    WriteValidStartupConfig(
        config_path,
        FindAvailablePort(),
        FindAvailablePort(),
        "[Grpc]\nChatDeadlineMs = 60001\n");
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {L"--config", config_path.wstring()}, fixture.Path());

    EXPECT_FALSE(result.timed_out) << result.stderr_text;
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("ChatDeadlineMs"), std::string::npos) << result.stderr_text;
}

// S01-BIND-01
/** 验证 TCP 端口占用在连接外部依赖前导致失败。 */
TEST(StartupConfigTests, OccupiedTcpPortFailsBeforeExternalDependencies) {
    TempDirectory fixture;
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor tcp_holder(context);
    const auto tcp_port = ReserveWildcardPort(tcp_holder);
    const auto grpc_port = FindAvailablePort();
    const auto config_path = fixture.Path() / "occupied-tcp.ini";
    WriteValidStartupConfig(config_path, tcp_port, grpc_port);
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {L"--config", config_path.wstring()}, fixture.Path());

    EXPECT_FALSE(result.timed_out) << "ChatServer did not fail fast for occupied TCP port " << tcp_port
        << "\nstderr: " << result.stderr_text << "\nstdout: " << result.stdout_text;
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("ChatServer startup error:"), std::string::npos) << result.stderr_text;
}

// S01-BIND-02
/** 验证 RPC 绑定失败会释放已绑定的 TCP 端口。 */
TEST(StartupConfigTests, OccupiedGrpcPortFailureReleasesTcpPort) {
    TempDirectory fixture;
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor grpc_holder(context);
    const auto grpc_port = ReservePort(grpc_holder);
    const auto tcp_port = FindAvailablePort();
    const auto config_path = fixture.Path() / "occupied-grpc.ini";
    WriteValidStartupConfig(config_path, tcp_port, grpc_port);
    ScopedEnvironmentValue environment(L"CHAT_CONFIG", std::nullopt);
    const auto executable = FindChatServerExecutable();
    ASSERT_TRUE(std::filesystem::is_regular_file(executable)) << executable.string();

    const auto result = RunProcess(executable, {L"--config", config_path.wstring()}, fixture.Path());

    EXPECT_FALSE(result.timed_out) << "ChatServer did not fail fast for occupied gRPC port " << grpc_port
        << "\nstderr: " << result.stderr_text << "\nstdout: " << result.stdout_text;
    EXPECT_NE(result.exit_code, 0U);
    EXPECT_NE(result.stderr_text.find("failed to listen on gRPC address"), std::string::npos) << result.stderr_text;

    boost::asio::io_context verification_context;
    boost::asio::ip::tcp::acceptor released_port(verification_context);
    boost::system::error_code error;
    released_port.open(boost::asio::ip::tcp::v4(), error);
    ASSERT_FALSE(error) << error.message();
    released_port.bind({boost::asio::ip::address_v4::loopback(), tcp_port}, error);
    EXPECT_FALSE(error) << "TCP port " << tcp_port << " was not released: " << error.message();
}
