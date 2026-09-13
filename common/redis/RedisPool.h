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
class RedisPool final {
public:
    using Milliseconds = std::chrono::milliseconds;
    using Clock = std::chrono::steady_clock;

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

    ~RedisPool() { Close(); }
    RedisPool(const RedisPool&) = delete;
    RedisPool& operator=(const RedisPool&) = delete;

    redisContext* Borrow(Milliseconds wait_timeout = Milliseconds(2000)) {
        if (wait_timeout <= Milliseconds::zero()) {
            return nullptr;
        }
        const auto deadline = Clock::now() + (std::min)(wait_timeout, Milliseconds(60000));
        Context connection(nullptr, &redisFree);
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (!_available.wait_until(lock, deadline, [this]() {
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

    static timeval Timeval(Milliseconds timeout) {
        const auto count = (std::max)(timeout.count(), Milliseconds::rep(1));
        timeval value = {};
        value.tv_sec = static_cast<long>(count / 1000);
        value.tv_usec = static_cast<long>((count % 1000) * 1000);
        return value;
    }

    static bool SetTimeout(redisContext* connection, Milliseconds timeout) {
        return redisSetTimeout(connection, Timeval(timeout)) == REDIS_OK;
    }

    Milliseconds Remaining(Clock::time_point deadline) const {
        return (std::min)(_io_timeout, std::chrono::duration_cast<Milliseconds>(deadline - Clock::now()));
    }

    bool Validate(redisContext* connection, Clock::time_point deadline) const {
        if (connection->err != 0 || Clock::now() >= deadline ||
            !SetTimeout(connection, Remaining(deadline))) {
            return false;
        }
        Reply reply(static_cast<redisReply*>(redisCommand(connection, "PING")), &freeReplyObject);
        return reply && reply->type == REDIS_REPLY_STATUS && reply->str != nullptr &&
            std::string(reply->str, reply->len) == "PONG";
    }

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
