#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>
#include <windows.h>

#include "varify.grpc.pb.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class OwnedProcess {
public:
    ~OwnedProcess() {
        Stop();
        CloseHandleIfValid(stdout_read_);
        CloseHandleIfValid(stdin_write_);
        CloseHandleIfValid(process_);
        CloseHandleIfValid(thread_);
    }

    bool Start(const std::filesystem::path& script) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE stdout_write = nullptr;
        HANDLE stdin_read = nullptr;
        if (!CreatePipe(&stdout_read_, &stdout_write, &security, 0) ||
            !SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0) ||
            !CreatePipe(&stdin_read, &stdin_write_, &security, 0) ||
            !SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandleIfValid(stdout_write);
            CloseHandleIfValid(stdin_read);
            return false;
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read;
        startup.hStdOutput = stdout_write;
        startup.hStdError = stdout_write;
        PROCESS_INFORMATION process_info{};
        std::wstring command = L"node.exe \"" + script.wstring() + L"\"";
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        const BOOL created = CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            script.parent_path().wstring().c_str(),
            &startup,
            &process_info
        );
        CloseHandleIfValid(stdout_write);
        CloseHandleIfValid(stdin_read);
        if (!created) {
            return false;
        }
        process_ = process_info.hProcess;
        thread_ = process_info.hThread;
        return true;
    }

    bool ReadReadyPort(int* port, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::string output;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD available = 0;
            if (!PeekNamedPipe(stdout_read_, nullptr, 0, nullptr, &available, nullptr)) {
                return false;
            }
            if (available > 0) {
                std::vector<char> buffer(available);
                DWORD read = 0;
                if (!ReadFile(stdout_read_, buffer.data(), available, &read, nullptr)) {
                    return false;
                }
                output.append(buffer.data(), read);
                const auto newline = output.find('\n');
                if (newline != std::string::npos) {
                    const std::string line = output.substr(0, newline);
                    if (line.rfind("READY ", 0) != 0) {
                        return false;
                    }
                    try {
                        *port = std::stoi(line.substr(6));
                    } catch (...) {
                        return false;
                    }
                    return *port > 0 && *port <= 65535;
                }
            }
            if (WaitForSingleObject(process_, 10) == WAIT_OBJECT_0) {
                return false;
            }
        }
        return false;
    }

    void Stop() {
        if (!process_) {
            return;
        }
        if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
            const char stop[] = "STOP\n";
            DWORD written = 0;
            WriteFile(stdin_write_, stop, sizeof(stop) - 1, &written, nullptr);
            CloseHandleIfValid(stdin_write_);
            if (WaitForSingleObject(process_, 2000) == WAIT_TIMEOUT) {
                TerminateProcess(process_, 3);
                WaitForSingleObject(process_, 2000);
            }
        }
    }

private:
    static void CloseHandleIfValid(HANDLE& handle) {
        if (handle) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }

    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE stdout_read_ = nullptr;
    HANDLE stdin_write_ = nullptr;
};

std::filesystem::path RepositoryRoot() {
    std::vector<wchar_t> path(MAX_PATH);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) {
        return {};
    }
    return std::filesystem::path(path.data())
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path();
}

TEST(CrossLanguageProtocolTests, CppClientCallsNodeVarifyOnDynamicLoopbackPort) {
    const auto script = RepositoryRoot() /
        "VarifyServer" / "test" / "protocol" / "node-varify-loopback-server.js";
    ASSERT_TRUE(std::filesystem::is_regular_file(script)) << script.string();

    OwnedProcess node_server;
    ASSERT_TRUE(node_server.Start(script));
    int port = 0;
    ASSERT_TRUE(node_server.ReadReadyPort(&port, std::chrono::seconds(3)));

    const auto channel = grpc::CreateChannel(
        "127.0.0.1:" + std::to_string(port),
        grpc::InsecureChannelCredentials()
    );
    const auto stub = message::VarifyService::NewStub(channel);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
    message::GetVarifyReq request;
    request.set_email("loopback-user@example.test");
    message::GetVarifyRsp response;

    const grpc::Status status = stub->GetVarifyCode(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(response.error(), 0);
    EXPECT_EQ(response.email(), "loopback-user@example.test");
    EXPECT_TRUE(response.code().empty());
}

} // namespace
