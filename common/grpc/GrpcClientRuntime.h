#pragma once

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace rpc {

enum class Failure {
    None,
    PoolExhausted,
    Closed,
    DeadlineExceeded,
    Unavailable,
    Cancelled,
    Other,
};

inline Failure ClassifyStatus(const grpc::Status& status) {
    if (status.ok()) {
        return Failure::None;
    }
    switch (status.error_code()) {
    case grpc::StatusCode::DEADLINE_EXCEEDED:
        return Failure::DeadlineExceeded;
    case grpc::StatusCode::UNAVAILABLE:
        return Failure::Unavailable;
    case grpc::StatusCode::CANCELLED:
        return Failure::Cancelled;
    default:
        return Failure::Other;
    }
}

inline std::chrono::milliseconds ParseDurationMs(
    const std::string& configured,
    const std::string& key,
    std::chrono::milliseconds default_value) {
    if (configured.empty()) {
        return default_value;
    }

    std::size_t parsed = 0;
    long long value = 0;
    try {
        value = std::stoll(configured, &parsed);
    }
    catch (const std::exception&) {
        throw std::invalid_argument(key + " must be an integer between 100 and 60000 milliseconds");
    }
    if (parsed != configured.size() || value < 100 || value > 60000) {
        throw std::invalid_argument(key + " must be an integer between 100 and 60000 milliseconds");
    }
    return std::chrono::milliseconds(value);
}

struct ClientPolicy {
    std::chrono::milliseconds acquire_timeout;
    std::chrono::milliseconds rpc_deadline;
};

template <typename Resource>
class BoundedPool {
public:
    using Factory = std::function<std::unique_ptr<Resource>()>;

    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : _owner(std::exchange(other._owner, nullptr)),
              _resource(std::move(other._resource)) {}

        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                Reset();
                _owner = std::exchange(other._owner, nullptr);
                _resource = std::move(other._resource);
            }
            return *this;
        }

        ~Lease() {
            Reset();
        }

        Resource* operator->() const { return _resource.get(); }
        Resource& operator*() const { return *_resource; }
        explicit operator bool() const { return _resource != nullptr; }

        void Reset() {
            if (_owner && _resource) {
                _owner->Return(std::move(_resource));
            }
            _owner = nullptr;
        }

    private:
        friend class BoundedPool<Resource>;
        Lease(BoundedPool* owner, std::unique_ptr<Resource> resource)
            : _owner(owner), _resource(std::move(resource)) {}

        BoundedPool* _owner = nullptr;
        std::unique_ptr<Resource> _resource;
    };

    struct AcquireResult {
        Failure failure = Failure::None;
        Lease lease;

        explicit operator bool() const {
            return failure == Failure::None && static_cast<bool>(lease);
        }
    };

    BoundedPool(std::size_t pool_size, std::chrono::milliseconds acquire_timeout, Factory factory)
        : _acquire_timeout(acquire_timeout) {
        if (pool_size == 0) {
            throw std::invalid_argument("gRPC pool size must be positive");
        }
        if (_acquire_timeout.count() <= 0) {
            throw std::invalid_argument("gRPC pool acquire timeout must be positive");
        }
        for (std::size_t i = 0; i < pool_size; ++i) {
            auto resource = factory();
            if (!resource) {
                throw std::invalid_argument("gRPC pool factory returned an empty resource");
            }
            _resources.push(std::move(resource));
        }
    }

    BoundedPool(const BoundedPool&) = delete;
    BoundedPool& operator=(const BoundedPool&) = delete;

    ~BoundedPool() {
        Close();
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_resources.empty()) {
            _resources.pop();
        }
    }

    AcquireResult Acquire() {
        std::unique_lock<std::mutex> lock(_mutex);
        const bool ready = _condition.wait_for(lock, _acquire_timeout, [this] {
            return _closed || !_resources.empty();
        });
        if (!ready) {
            return { Failure::PoolExhausted, Lease() };
        }
        if (_closed) {
            return { Failure::Closed, Lease() };
        }

        auto resource = std::move(_resources.front());
        _resources.pop();
        return { Failure::None, Lease(this, std::move(resource)) };
    }

    void Close() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _closed = true;
        }
        _condition.notify_all();
    }

private:
    void Return(std::unique_ptr<Resource> resource) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_closed) {
                return;
            }
            _resources.push(std::move(resource));
        }
        _condition.notify_one();
    }

    std::chrono::milliseconds _acquire_timeout;
    std::queue<std::unique_ptr<Resource>> _resources;
    std::condition_variable _condition;
    std::mutex _mutex;
    bool _closed = false;
};

template <typename Response>
struct CallResult {
    Response response;
    Failure failure = Failure::None;

    explicit operator bool() const { return failure == Failure::None; }
};

template <typename Pool, typename Request, typename Response, typename Invoker>
CallResult<Response> InvokeUnary(
    Pool& pool,
    const Request& request,
    std::chrono::milliseconds deadline,
    Invoker invoke) {
    auto acquired = pool.Acquire();
    if (!acquired) {
        return { Response(), acquired.failure };
    }

    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + deadline);
    Response response;
    const auto status = invoke(*acquired.lease, context, request, response);
    return { std::move(response), ClassifyStatus(status) };
}

}  // namespace rpc
