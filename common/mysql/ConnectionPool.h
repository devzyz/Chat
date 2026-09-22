#pragma once

#include <jdbc/cppconn/connection.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chat_mysql {

// Connection must provide the JDBC health/transaction operations. The template
// permits deterministic lifecycle tests; production uses sql::Connection only.
// Factory and validation I/O must have finite connector timeouts. A borrow's
// deadline bounds queue waiting; an in-flight synchronous call cannot be cancelled.
template <typename Connection = sql::Connection>
class ConnectionPool final {
public:
    using Clock = std::chrono::steady_clock;
    using Factory = std::function<std::unique_ptr<Connection>()>;

    class Lease {
    public:
        Lease(ConnectionPool& pool, std::unique_ptr<Connection> connection)
            : _pool(pool), _connection(std::move(connection)) {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : _pool(other._pool), _connection(std::move(other._connection)) {}
        ~Lease() { if (_connection) _pool.Return(std::move(_connection)); }
        Connection* operator->() const { return _connection.get(); }
        Connection& operator*() const { return *_connection; }
    private:
        ConnectionPool& _pool;
        std::unique_ptr<Connection> _connection;
    };

    ConnectionPool(int capacity, Factory factory) : _capacity(capacity), _factory(std::move(factory)) {
        if (capacity < 0 || !_factory) throw std::invalid_argument("invalid MySQL pool configuration");
        _idle.reserve(static_cast<std::size_t>(capacity));
        for (int index = 0; index < capacity; ++index) {
            auto connection = _factory();
            if (!connection) throw std::runtime_error("MySQL connection unavailable");
            _idle.push_back(std::move(connection));
            ++_size;
        }
    }
    ~ConnectionPool() { Close(); }
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    std::unique_ptr<Connection> Borrow(Clock::time_point deadline = Clock::now() + std::chrono::seconds(2)) {
        std::unique_ptr<Connection> connection;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (Clock::now() >= deadline || !_available.wait_until(lock, deadline, [this] {
                return _closed || !_idle.empty() || _size < _capacity;
            }) || _closed) return nullptr;
            if (!_idle.empty()) {
                connection = std::move(_idle.back());
                _idle.pop_back();
            } else {
                ++_size;
            }
        }
        // One replacement attempt, only before handing the connection to business
        // code. Never replay SQL writes after an ambiguous transport failure.
        try {
            if (connection && !connection->isValid()) connection.reset();
        } catch (...) { connection.reset(); }
        try {
            if (!connection && Clock::now() < deadline) connection = _factory();
        } catch (...) { connection.reset(); }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (connection && !_closed && Clock::now() < deadline) return connection;
            --_size;
        }
        _available.notify_all();
        return nullptr;
    }

    Lease Acquire() {
        auto connection = Borrow();
        if (!connection) throw std::runtime_error("MySQL pool unavailable");
        return Lease(*this, std::move(connection));
    }

    // Null means that a checked-out connection was explicitly invalidated.
    void Return(std::unique_ptr<Connection> connection) noexcept {
        try {
            if (connection && connection->isClosed()) connection.reset();
            if (connection && !connection->getAutoCommit()) {
                connection->rollback();
                connection->setAutoCommit(true);
            }
        } catch (...) {
            // Rollback/reset failure makes this session unsafe for another borrower.
            connection.reset();
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (connection && !_closed) _idle.push_back(std::move(connection));
            else --_size;
        }
        _available.notify_all();
    }

    void Close() {
        std::vector<std::unique_ptr<Connection>> closing;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_closed) return;
            _closed = true;
            _size -= static_cast<int>(_idle.size());
            closing.swap(_idle);
        }
        _available.notify_all();
        // Socket destruction is outside the mutex. Leases must outlive their work
        // and be returned before the pool itself is destroyed.
    }

private:
    const int _capacity;
    Factory _factory;
    std::mutex _mutex;
    std::condition_variable _available;
    std::vector<std::unique_ptr<Connection>> _idle;
    int _size = 0;
    bool _closed = false;
};

} // namespace chat_mysql
