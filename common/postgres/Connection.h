#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <cerrno>
#include <poll.h>
#endif
#include <libpq-fe.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace chat_pg {
// 配置来自服务启动入口。database 为数据库名，业务对象统一在 chat 命名空间。
struct Settings {
    std::string host, port, database, user, password;
    int connect_seconds = 2;
    int io_seconds = 2;
};

// 仅携带 SQLSTATE；原生诊断可能包含业务记录，不能传播到日志。
class Error : public std::runtime_error {
public:
    // 仅保存可公开分类的 SQLSTATE。
    explicit Error(std::string code) : std::runtime_error("PostgresOperationFailed"), state(std::move(code)) {}

    std::string state;
};

using Rows = std::unique_ptr<PGresult, decltype(&PQclear)>;
using Deadline = std::chrono::steady_clock::time_point;

// 将可包含零字节的值绑定到 decode($n,'hex')，不改变业务正文。
inline std::string Hex(const std::string& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

// 独占物理连接；一次只允许一个工作线程使用。超时或协议状态未知即断开。
class Connection final {
public:
    // 建立有限时长的原生连接，使用扩展协议绑定参数，禁止自动重放。
    explicit Connection(const Settings& config) : io_seconds_(config.io_seconds) {
        const auto timeout = std::to_string(config.connect_seconds);
        const char* names[] = {"host", "port", "dbname", "user", "password", "connect_timeout", "client_encoding",
            nullptr};
        const char* values[] = {config.host.c_str(), config.port.c_str(), config.database.c_str(), config.user.c_str(),
            config.password.c_str(), timeout.c_str(), "UTF8", nullptr};
        connection_.reset(PQconnectStartParams(names, values, 0));
        if (!connection_) throw Error("08001");
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(config.connect_seconds);
        for (;;) {
            const auto status = PQconnectPoll(connection_.get());
            if (status == PGRES_POLLING_OK) break;
            if (status != PGRES_POLLING_READING && status != PGRES_POLLING_WRITING) throw Error("08001");
            Wait(status == PGRES_POLLING_WRITING, end);
        }
        if (PQsetnonblocking(connection_.get(), 1) != 0) throw Error("08001");
        Query("SET search_path = pg_catalog, chat");
        Query("SET TIME ZONE 'UTC'");
        Query("SET lock_timeout = '2s'");
    }

    // 执行单条参数化语句并消费所有协议结果。COMMIT 回包丢失不会重试。
    Rows Query(const char* sql, const std::vector<std::string>& args = {}, Deadline deadline = (Deadline::max)()) {
        const auto end = (std::min)(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(io_seconds_));
        if (std::chrono::steady_clock::now() >= end) throw Error("57014");
        std::vector<const char*> values;
        for (const auto& arg : args) {
            if (arg.find('\0') != std::string::npos) throw Error("22021");
            values.push_back(arg.c_str());
        }
        if (!connection_ || !PQsendQueryParams(connection_.get(), sql, static_cast<int>(values.size()), nullptr,
                                values.data(), nullptr, nullptr, 0)) {
            Disconnect("08006");
        }
        Rows rows(nullptr, PQclear);
        for (;;) {
            const int pending = PQflush(connection_.get());
            if (pending == 0) break;
            if (pending < 0) Disconnect("08006");
            Wait(true, end);
        }
        bool committed = false;
        try {
            for (;;) {
                while (PQisBusy(connection_.get())) {
                    Wait(false, end);
                    if (!PQconsumeInput(connection_.get())) Disconnect("08006");
                }
                PGresult* next = PQgetResult(connection_.get());
                if (!next) break;
                rows.reset(next);
                committed = PQresultStatus(next) == PGRES_COMMAND_OK && std::strcmp(PQcmdStatus(next), "COMMIT") == 0;
            }
        } catch (const Error&) {
            // 已收到原生 COMMIT 确认，清理阶段的断连不能撤销成功。
            if (committed) return rows;
            throw;
        }
        if (!rows) Disconnect("08006");
        const auto status = PQresultStatus(rows.get());
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            const char* state = PQresultErrorField(rows.get(), PG_DIAG_SQLSTATE);
            throw Error(state ? state : "08006");
        }
        if (std::strcmp(sql, "COMMIT") == 0 && !committed) throw Error("40000");
        return rows;
    }

    // 借出前用真实往返检测静默断开的空闲连接。
    bool IsValid() {
        return !IsClosed() && PQntuples(Query("SELECT 1").get()) == 1;
    }

    // 供有界池在归还时检查是否应销毁连接。
    bool IsClosed() const {
        return !connection_ || PQstatus(connection_.get()) != CONNECTION_OK;
    }

    // libpq 自动提交对应于没有活动事务。
    bool GetAutoCommit() const {
        return connection_ && PQtransactionStatus(connection_.get()) == PQTRANS_IDLE;
    }

    // 在归还前显式回滚，失败抛出以使池销毁连接。
    void Rollback() {
        Query("ROLLBACK");
    }

    // 与已有池的复位协议对接，回滚后确认物理连接已空闲。
    void SetAutoCommit(bool enabled) {
        if (!enabled || !GetAutoCommit()) throw Error("25000");
    }

    // 令未确认释放的会话级锁随物理连接关闭。
    void Close() {
        connection_.reset();
    }

private:
    // 使不确定的连接无法被下一个请求继续使用。
    [[noreturn]] void Disconnect(const char* state) {
        Close();
        throw Error(state);
    }

    // 等待当前 socket 就绪，单调时钟覆盖网络黑洞与服务端阻塞。
    void Wait(bool write, Deadline end) {
        const int descriptor = PQsocket(connection_.get());
        if (descriptor < 0) Disconnect("08006");
        for (;;) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0) Disconnect("57014");
#ifdef _WIN32
            WSAPOLLFD fd{static_cast<SOCKET>(descriptor), static_cast<short>(write ? POLLWRNORM : POLLRDNORM), 0};
            const int ready = WSAPoll(&fd, 1, static_cast<int>(left.count()));
#else
            pollfd fd{descriptor, static_cast<short>(write ? POLLOUT : POLLIN), 0};
            const int ready = poll(&fd, 1, static_cast<int>(left.count()));
            if (ready < 0 && errno == EINTR) continue;
#endif
            if (ready == 0) Disconnect("57014");
            if (ready < 0) Disconnect("08006");
            return;
        }
    }

    int io_seconds_;
    std::unique_ptr<PGconn, decltype(&PQfinish)> connection_{nullptr, PQfinish};
};
} // namespace chat_pg
