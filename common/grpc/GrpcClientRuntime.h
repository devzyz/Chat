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

/** @brief 把 gRPC 状态转换为本项目传输失败枚举，业务错误仍由响应解释。 */
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

/** @brief 解析 100～60000 毫秒配置，空值用默认值，非整数或越界抛异常。 */
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

/** @brief 保存 gRPC 池借用等待时长与每次 RPC 的毫秒截止时间。 */
struct ClientPolicy {
    std::chrono::milliseconds acquire_timeout;
    std::chrono::milliseconds rpc_deadline;
};

/** @brief 在线程间分配固定容量的独占 RPC 资源；关闭唤醒等待者，租约须在池析构前释放。 */
template <typename Resource>
class BoundedPool {
public:
    using Factory = std::function<std::unique_ptr<Resource>()>;

    /** @brief 持有一次独占资源借用并在销毁时归还；被引用的池必须比租约存活更久。 */
    class Lease {
    public:
        /** @brief 创建不关联池且不持有资源的空租约。 */
        Lease() = default;
        /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
        Lease(const Lease&) = delete;
        /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
        Lease& operator=(const Lease&) = delete;

        /** @brief 接管来源的池关联与独占资源，移动后来源为空租约。 */
        Lease(Lease&& other) noexcept
            : _owner(std::exchange(other._owner, nullptr)),
              _resource(std::move(other._resource)) {}

        /** @brief 先归还自身资源，再接管来源并使其为空；自移动不改变状态。 */
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                Reset();
                _owner = std::exchange(other._owner, nullptr);
                _resource = std::move(other._resource);
            }
            return *this;
        }

        /** @brief 将仍持有的资源归还其池；池必须仍然存活。 */
        ~Lease() {
            Reset();
        }

        /** @brief 返回租约内资源的借用访问，调用前须保证租约有效且尚未归还。 */
        Resource* operator->() const { return _resource.get(); }
        /** @brief 返回租约内资源的借用访问，调用前须保证租约有效且尚未归还。 */
        Resource& operator*() const { return *_resource; }
        /** @brief 查询租约是否仍持有资源。 */
        explicit operator bool() const { return _resource != nullptr; }

        /** @brief 归还仍持有的租约资源并清空池关联，可重复调用。 */
        void Reset() {
            if (_owner && _resource) {
                _owner->Return(std::move(_resource));
            }
            _owner = nullptr;
        }

    private:
        friend class BoundedPool<Resource>;
        /** @brief 接管池借出的独占资源并记录归还目标；池须活过租约。 */
        Lease(BoundedPool* owner, std::unique_ptr<Resource> resource)
            : _owner(owner), _resource(std::move(resource)) {}

        BoundedPool* _owner = nullptr;
        std::unique_ptr<Resource> _resource;
    };

    /** @brief 同时保存借用失败分类和可能有效的资源租约。 */
    struct AcquireResult {
        Failure failure = Failure::None;
        Lease lease;

        /** @brief 查询当前结果或租约是否具有可用的成功状态。 */
        explicit operator bool() const {
            return failure == Failure::None && static_cast<bool>(lease);
        }
    };

    /** @brief 校验正容量和借用期限，并通过工厂建立固定数量资源；失败抛异常。 */
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

    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    BoundedPool(const BoundedPool&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    BoundedPool& operator=(const BoundedPool&) = delete;

    /** @brief 关闭池并释放空闲资源；调用方须先结束使用并归还所有借出的资源。 */
    ~BoundedPool() {
        Close();
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_resources.empty()) {
            _resources.pop();
        }
    }

    /** @brief 在配置期限内等待资源，返回可移动租约或耗尽/关闭分类。 */
    AcquireResult Acquire() {
        std::unique_lock<std::mutex> lock(_mutex);
        const bool ready = _condition.wait_for(lock, _acquire_timeout, /** @brief 在关闭或有可用 stub 时唤醒等待借用者。 */ [this] {
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

    /** @brief 禁止后续借用并唤醒等待者；空闲资源留到析构释放，借出资源归还时释放。 */
    void Close() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _closed = true;
        }
        _condition.notify_all();
    }

private:
    /** @brief 将租约资源放回开放池并唤醒等待者；池已关闭时释放资源。 */
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

/** @brief 携带 RPC 响应及传输失败分类；业务错误仍由响应字段解释。 */
template <typename Response>
struct CallResult {
    Response response;
    Failure failure = Failure::None;

    /** @brief 查询当前结果或租约是否具有可用的成功状态。 */
    explicit operator bool() const { return failure == Failure::None; }
};

/** @brief 先有界借用 stub，再设置 RPC 截止时间执行一次调用；返回响应及失败分类，不重放请求。 */
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
