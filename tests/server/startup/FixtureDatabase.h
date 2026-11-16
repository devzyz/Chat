#pragma once

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace startup_fixture {
// 正常启动用例只连接夹具的回环端口，密码由父进程环境传递。
inline std::string DatabaseSection() {
    const char* raw = std::getenv("CHAT_STARTUP_PG_PORT");
    const std::string port = raw ? raw : "";
    unsigned int parsed = 0;
    const auto result = std::from_chars(port.data(), port.data() + port.size(), parsed);
    if (result.ec != std::errc() || result.ptr != port.data() + port.size() || parsed == 0 || parsed > 65535)
        throw std::runtime_error("OwnedStartupPostgresRequired");
    return "[Database]\nDriver=postgresql\nHost=127.0.0.1\nPort=" + port + "\nUser=chat_fixture\nSchema=postgres\n";
}
} // namespace startup_fixture
