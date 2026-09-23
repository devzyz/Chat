#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <hiredis/hiredis.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace chat_redis {

// Owns only transport resources, never business key construction. Connections are
// established lazily and validated on checkout, so there is no heartbeat thread
// whose shutdown can outlive the pool. Callers must return leases before destruction.
/** @brief 惰性建立并校验 Redis 连接；无保活线程，坏连接最多替换一次，业务命令不重放。 */
class RedisPool final {
public:
    using Milliseconds = std::chrono::milliseconds;
    using Clock = std::chrono::steady_clock;

    /** @brief 保存容量和认证配置，验证字面量端点与有限期限；连接在借用时惰性建立。 */
    RedisPool(std::string host, int port, std::string password, std::size_t capacity,
        Milliseconds io_timeout = Milliseconds(2000))
        : _host(host == "localhost" ? "127.0.0.1" : std::move(host)), _port(port),
          _password(std::move(password)), _capacity(capacity), _io_timeout(io_timeout) {
        // Synchronous DNS is not bounded by hiredis' socket deadline. Accept literal
        // endpoints (and loopback localhost) instead of claiming a false DNS bound.
        unsigned char address[16] = {};
        if (_port < 1 || _port > 65535 || _io_timeout <= Milliseconds::zero() ||
            _io_timeout > Milliseconds(60000) ||
            (inet_pton(AF_INET, _host.c_str(), address) != 1 &&
             inet_pton(AF_INET6, _host.c_str(), address) != 1)) {
            throw std::invalid_argument("Redis requires a literal endpoint and a finite timeout");
        }
    }

    /** @brief 关闭池并释放空闲资源；调用方须先结束使用并归还所有借出的资源。 */
    ~RedisPool() { Close(); }
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    RedisPool(const RedisPool&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    RedisPool& operator=(const RedisPool&) = delete;

    /** @brief 在有限等待内借出已认证且健康的原始连接，失败返回 nullptr；调用方须用 Return 归还。 */
    redisContext* Borrow(Milliseconds wait_timeout = Milliseconds(2000)) {
        if (wait_timeout <= Milliseconds::zero()) {
            return nullptr;
        }
        const auto deadline = Clock::now() + (std::min)(wait_timeout, Milliseconds(60000));
        Context connection(nullptr, &redisFree);
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (!_available.wait_until(lock, deadline, /** @brief 在关闭、有空闲连接或尚有容量时结束借用等待。 */ [this]() {
                    return _is_closed || !_idle.empty() || _size < _capacity;
                }) || _is_closed) {
                return nullptr;
            }
            if (!_idle.empty()) {
                connection = std::move(_idle.front());
                _idle.pop();
            } else {
                ++_size; // Reserve before releasing the lock for network I/O.
            }
        }

        if (connection && !Validate(connection.get(), deadline)) {
            connection.reset();
        }
        // At most one replacement attempt per borrow. Never replay business commands.
        if (!connection && Clock::now() < deadline) {
            connection = Connect(deadline);
        }
        if (connection && !SetTimeout(connection.get(), _io_timeout)) {
            connection.reset();
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!connection || _is_closed || Clock::now() >= deadline) {
                connection.reset();
                --_size;
                _available.notify_all();
                return nullptr;
            }
        }
        return connection.release();
    }

    /** @brief 接回原始连接所有权，错误或关闭后的连接直接释放，唤醒等待借用者。 */
    void Return(redisContext* raw_connection) {
        if (raw_connection == nullptr) {
            return;
        }
        Context connection(raw_connection, &redisFree);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_is_closed || connection->err != 0) {
                --_size;
            } else {
                _idle.push(std::move(connection));
            }
        }
        _available.notify_all();
    }

    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close() {
        std::queue<Context> idle;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _is_closed = true;
            _size -= _idle.size();
            _idle.swap(idle);
        }
        _available.notify_all();
        // Closing sockets and freeing contexts is outside the pool mutex.
    }

private:
    using Context = std::unique_ptr<redisContext, decltype(&redisFree)>;
    using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

    /** @brief 把毫秒期限转换为至少一毫秒的 socket timeval。 */
    static timeval Timeval(Milliseconds timeout) {
        const auto count = (std::max)(timeout.count(), Milliseconds::rep(1));
        timeval value = {};
        value.tv_sec = static_cast<long>(count / 1000);
        value.tv_usec = static_cast<long>((count % 1000) * 1000);
        return value;
    }

    /** @brief 更新 hiredis 连接 I/O 期限，返回设置是否成功。 */
    static bool SetTimeout(redisContext* connection, Milliseconds timeout) {
        return redisSetTimeout(connection, Timeval(timeout)) == REDIS_OK;
    }

    /** @brief 返回总截止时间与单次 I/O 上限之间较短的剩余毫秒数。 */
    Milliseconds Remaining(Clock::time_point deadline) const {
        return (std::min)(_io_timeout, std::chrono::duration_cast<Milliseconds>(deadline - Clock::now()));
    }

    /** @brief 在给定期限内通过 PING/PONG 校验 Redis 连接健康。 */
    bool Validate(redisContext* connection, Clock::time_point deadline) const {
        if (connection->err != 0 || Clock::now() >= deadline ||
            !SetTimeout(connection, Remaining(deadline))) {
            return false;
        }
        Reply reply(static_cast<redisReply*>(redisCommand(connection, "PING")), &freeReplyObject);
        return reply && reply->type == REDIS_REPLY_STATUS && reply->str != nullptr &&
            std::string(reply->str, reply->len) == "PONG";
    }

    /** @brief 在截止时间内建立 Redis 连接、按需认证并校验健康，失败返回空连接。 */
    Context Connect(Clock::time_point deadline) const {
        Context connection(redisConnectWithTimeout(_host.c_str(), _port, Timeval(Remaining(deadline))), &redisFree);
        if (!connection || connection->err != 0 || Clock::now() >= deadline ||
            !SetTimeout(connection.get(), Remaining(deadline))) {
            return Context(nullptr, &redisFree);
        }
        if (!_password.empty()) {
            Reply reply(static_cast<redisReply*>(redisCommand(connection.get(), "AUTH %b",
                _password.data(), _password.size())), &freeReplyObject);
            if (!reply || reply->type != REDIS_REPLY_STATUS || reply->str == nullptr ||
                std::string(reply->str, reply->len) != "OK") {
                return Context(nullptr, &redisFree);
            }
        }
        if (!Validate(connection.get(), deadline)) {
            return Context(nullptr, &redisFree);
        }
        return connection;
    }

    const std::string _host;
    const int _port;
    const std::string _password;
    const std::size_t _capacity;
    const Milliseconds _io_timeout;
    std::mutex _mutex;
    std::condition_variable _available;
    std::queue<Context> _idle;
    std::size_t _size = 0;
    bool _is_closed = false;
};

} // namespace chat_redis
