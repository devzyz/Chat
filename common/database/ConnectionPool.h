#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace database {

// 新的原生会话适配相同的有界连接池。
struct SessionPolicy {
    // 通过各驱动的有限时长查询校验连接。
    template <class T> static bool Healthy(T& db) {
        return db.IsValid();
    }

    // 清理未提交事务，丢弃失去同步或已关闭的物理连接。
    template <class T> static bool Reset(T& db) {
        if (db.IsClosed()) return false;
        if (!db.GetAutoCommit()) {
            db.Rollback();
            db.SetAutoCommit(true);
        }
        return true;
    }
};

// Factory and validation I/O must have finite connector timeouts. A borrow's
// deadline bounds queue waiting; an in-flight synchronous call cannot be cancelled.
/** @brief 有界独占数据库连接池；网络校验在锁外，借用截止时间不能取消已经开始的同步 I/O。 */
template <typename Connection, typename Policy = SessionPolicy> class ConnectionPool final {
public:
    using Clock = std::chrono::steady_clock;
    using Factory = std::function<std::unique_ptr<Connection>()>;

    /** @brief 持有一次独占资源借用并在销毁时归还；被引用的池必须比租约存活更久。 */
    class Lease {
    public:
        /** @brief 接管传入连接并绑定归还池；pool 须比租约存活更久。 */
        Lease(ConnectionPool& pool, std::unique_ptr<Connection> connection)
            : _pool(pool), _connection(std::move(connection)) {}

        /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
        Lease(const Lease&) = delete;
        /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
        Lease& operator=(const Lease&) = delete;

        /** @brief 接管来源租约的连接并沿用其池，移动后来源不再持有连接。 */
        Lease(Lease&& other) noexcept : _pool(other._pool), _connection(std::move(other._connection)) {}

        /** @brief 将仍持有的资源归还其池；池必须仍然存活。 */
        ~Lease() {
            if (_connection) _pool.Return(std::move(_connection));
        }

        /** @brief 返回租约内资源的借用访问，调用前须保证租约有效且尚未归还。 */
        Connection* operator->() const {
            return _connection.get();
        }

        /** @brief 返回租约内资源的借用访问，调用前须保证租约有效且尚未归还。 */
        Connection& operator*() const {
            return *_connection;
        }

    private:
        ConnectionPool& _pool;
        std::unique_ptr<Connection> _connection;
    };

    /** @brief 校验容量及工厂并预建连接，非法配置或创建失败抛异常。 */
    ConnectionPool(int capacity, Factory factory) : _capacity(capacity), _factory(std::move(factory)) {
        if (capacity < 0 || !_factory) throw std::invalid_argument("invalid database pool configuration");
        _idle.reserve(static_cast<std::size_t>(capacity));
        for (int index = 0; index < capacity; ++index) {
            auto connection = _factory();
            if (!connection) throw std::runtime_error("database connection unavailable");
            _idle.push_back(std::move(connection));
            ++_size;
        }
    }

    /** @brief 关闭池并释放空闲资源；调用方须先结束使用并归还所有借出的资源。 */
    ~ConnectionPool() {
        Close();
    }

    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    ConnectionPool(const ConnectionPool&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    /** @brief 等待至截止时间借出已校验连接，必要时仅尝试替换一次；关闭或失败返回空指针，同步 I/O 不可被截止时间中断。 */
    std::unique_ptr<Connection> Borrow(Clock::time_point deadline = Clock::now() + std::chrono::seconds(2)) {
        std::unique_ptr<Connection> connection;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (Clock::now() >= deadline ||
                !_available.wait_until(lock, deadline,
                    // 仅在关闭、可借用或具备剩余容量时唤醒。
                    [this] {
                        return _closed || !_idle.empty() || _size < _capacity;
                    }) ||
                _closed) {
                return nullptr;
            }
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
            if (connection && !Policy::Healthy(*connection)) connection.reset();
        } catch (...) {
            connection.reset();
        }
        try {
            if (!connection && Clock::now() < deadline) connection = _factory();
        } catch (...) {
            connection.reset();
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (connection && !_closed && Clock::now() < deadline) return connection;
            --_size;
        }
        _available.notify_all();
        return nullptr;
    }

    /** @brief 按默认期限借用连接并包装为自动归还租约，失败抛异常。 */
    Lease Acquire() {
        auto connection = Borrow();
        if (!connection) throw std::runtime_error("database pool unavailable");
        return Lease(*this, std::move(connection));
    }

    // Null means that a checked-out connection was explicitly invalidated.
    /** @brief 归还独占连接，先回滚未提交事务并恢复自动提交；清理失败丢弃连接且不抛异常。 */
    void Return(std::unique_ptr<Connection> connection) noexcept {
        try {
            if (connection && !Policy::Reset(*connection)) connection.reset();
        } catch (...) {
            // Rollback/reset failure makes this session unsafe for another borrower.
            connection.reset();
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (connection && !_closed) {
                _idle.push_back(std::move(connection));
            } else {
                --_size;
            }
        }
        _available.notify_all();
    }

    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
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

} // namespace database
