#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

enum class ExternalTask { Redis, MySQL, OutboundGrpc };
enum class LogicSubmitResult { Accepted, Full, Closed };
enum class RedisAdmission { Critical, Routing };

// 进程级依赖执行器；各依赖有独立等待队列，MySQL 的执行名额可在 Redis 复核期间预留。
class DependencyExecutor final {
public:
    using Task = std::function<void()>;
    // 查库前的复核在 Redis 池运行；拒绝或异常时只调用 denied。
    struct SqlGuard {
        std::function<bool()> check;
        Task denied;
    };
    // 容量不含执行中的任务；默认保留八个 Redis 等待位置给关键请求。
    explicit DependencyExecutor(std::size_t mysql_workers = 2, std::size_t capacity = 64);
    // 排空任务并回收所有线程，所有者须先停止生产者。
    ~DependencyExecutor();
    // 标识当前调用的实际依赖工作线程，适配器可在执行 I/O 前检查所属池。
    static bool RunningOn(ExternalTask kind) {
        return _current == kind;
    }
    // 统一投递；受理后恰好执行任务或其 SQL 拒绝回调。普通路由只可使用 Redis 非预留容量。
    LogicSubmitResult SubmitExternal(
        ExternalTask kind,
        Task task,
        RedisAdmission admission = RedisAdmission::Critical,
        SqlGuard guard = {});
    // 为主线程、同步入站 RPC 或生命周期协调线程提供结果；依赖 worker 和会话 strand 不等待此 future。
    template <class F>
    auto SubmitFuture(ExternalTask kind, F work, RedisAdmission admission = RedisAdmission::Critical) {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(work));
        auto result = task->get_future();
        // packaged_task 将结果或异常交还原调用方，执行器不持有调用方的栈引用。
        auto invoke = [task] {
            (*task)();
        };
        if (SubmitExternal(kind, std::move(invoke), admission) != LogicSubmitResult::Accepted)
            throw std::runtime_error("dependency task not admitted");
        return result;
    }
    // 由非 worker 所有者在停止生产后等待排空；随后仍可投递最后的实例注销。
    void Drain();
    // 仅由非 worker 所有者关闭入口并等待线程退出，重复调用安全。
    void Stop();

private:
    inline static thread_local std::optional<ExternalTask> _current;
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
