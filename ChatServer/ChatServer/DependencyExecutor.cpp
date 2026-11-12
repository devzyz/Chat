#include "DependencyExecutor.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 同一把短锁保护入队、名额预留和关闭，所有阻塞 I/O 都在锁外执行。
struct DependencyExecutor::Impl {
    // 一笔待执行任务；guard 仅用于已认证的 SQL。
    struct Job {
        Task work;
        SqlGuard guard;
        bool routing = false;
    };
    // active 包括正在 Redis 复核的 SQL 名额；approved 不再参与普通排队。
    struct Pool {
        std::condition_variable ready;
        std::queue<Job> waiting;
        std::queue<Task> approved;
        std::vector<std::thread> workers;
        std::size_t active = 0;
        std::size_t rejected = 0;
        std::size_t routing_waiting = 0;
        std::size_t limit;
        // 固定执行名额，启动期间也不读取正在增长的线程数组。
        Pool(ExternalTask kind, std::size_t count) : limit(count), kind(kind) {}
        const ExternalTask kind;
    };
    // 初始化容量后启动三组消费者；构造失败时回收已启动部分。
    Impl(std::size_t count, std::size_t capacity)
        : mysql(ExternalTask::MySQL, count), capacity(capacity),
          routing_capacity(capacity - std::min<std::size_t>(8, capacity / 2)) {
        if (!capacity || !count)
            throw std::invalid_argument("invalid dependency capacity");
        try {
            for (auto pool : {&redis, &mysql, &grpc})
                for (std::size_t i = 0; i < pool->limit; ++i)
                    pool->workers.emplace_back(&Impl::Run, this, std::ref(*pool));
        } catch (...) {
            Stop();
            throw;
        }
    }
    // 在锁内选择固定池。
    Pool& Select(ExternalTask kind) {
        switch (kind) {
        case ExternalTask::Redis:
            return redis;
        case ExternalTask::MySQL:
            return mysql;
        case ExternalTask::OutboundGrpc:
            return grpc;
        }
        throw std::invalid_argument("unknown dependency");
    }
    // 内部的写前复核属于已受理的 SQL，关闭入口后仍允许完成。
    LogicSubmitResult Enqueue(ExternalTask kind, Job job, RedisAdmission admission, bool internal = false) {
        auto& pool = Select(kind);
        std::unique_lock<std::mutex> lock(mutex);
        if (closed && !internal)
            return LogicSubmitResult::Closed;
        job.routing = kind == ExternalTask::Redis && admission == RedisAdmission::Routing;
        if (pool.waiting.size() >= capacity || (job.routing && pool.routing_waiting >= routing_capacity)) {
            const auto rejected = ++pool.rejected;
            lock.unlock();
            if ((rejected & (rejected - 1)) == 0)
                SPDLOG_WARN("dependency queue full, kind={}, rejected={}", static_cast<int>(kind), rejected);
            return LogicSubmitResult::Full;
        }
        const bool routing = job.routing;
        pool.waiting.push(std::move(job));
        if (routing)
            ++pool.routing_waiting;
        ++outstanding;
        lock.unlock();
        pool.ready.notify_one();
        return LogicSubmitResult::Accepted;
    }
    // 回填已占用的 MySQL 名额；成功直接运行 SQL，失败只完成请求。
    void Approve(Task task) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            mysql.approved.push(std::move(task));
        }
        mysql.ready.notify_one();
    }
    // 把凭据检查交给 Redis；SQL 线程立刻回到取任务循环。
    void CheckSql(Job job) {
        auto denied = job.guard.denied;
        // Redis 出错与拒绝均回收原名额。
        auto check = [this, job = std::move(job)]() mutable {
            bool valid = false;
            try {
                valid = job.guard.check();
            } catch (...) {
                SPDLOG_WARN("SQL credential check failed");
            }
            Approve(valid ? std::move(job.work) : std::move(job.guard.denied));
        };
        if (Enqueue(ExternalTask::Redis, {std::move(check), {}}, RedisAdmission::Critical, true) !=
            LogicSubmitResult::Accepted)
            Approve(std::move(denied));
    }
    // 预留任务优先，普通任务只在空闲名额存在时出队。
    void Run(Pool& pool) {
        _current = pool.kind;
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                // 在途检查未结束时不能因关闭提前退出。
                pool.ready.wait(lock, [&] {
                    return !pool.approved.empty() || (!pool.waiting.empty() && pool.active < pool.limit) ||
                           (closed && outstanding == 0);
                });
                if (!pool.approved.empty()) {
                    job.work = std::move(pool.approved.front());
                    pool.approved.pop();
                } else if (!pool.waiting.empty()) {
                    job = std::move(pool.waiting.front());
                    pool.waiting.pop();
                    if (job.routing)
                        --pool.routing_waiting;
                    ++pool.active;
                } else
                    return;
            }
            if (job.guard.check) {
                CheckSql(std::move(job));
                continue;
            }
            try {
                job.work();
            } catch (...) {
                SPDLOG_ERROR("dependency task failed");
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                --pool.active;
                if (--outstanding == 0) {
                    idle.notify_all();
                    if (closed)
                        WakeAll();
                }
            }
            pool.ready.notify_one();
        }
    }
    // 所有消费者重新检查退出条件。
    void WakeAll() {
        for (auto pool : {&redis, &mysql, &grpc})
            pool->ready.notify_all();
    }
    // 对外关闭入口后排空，包括内部 Redis 复核。
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            closed = true;
        }
        WakeAll();
        for (auto pool : {&redis, &mysql, &grpc})
            for (auto& thread : pool->workers)
                thread.join();
    }
    Pool redis{ExternalTask::Redis, 2}, mysql, grpc{ExternalTask::OutboundGrpc, 1};
    const std::size_t capacity, routing_capacity;
    std::mutex mutex;
    std::condition_variable idle;
    std::size_t outstanding = 0;
    bool closed = false;
    std::once_flag stop_once;
};

DependencyExecutor::DependencyExecutor(std::size_t mysql_workers, std::size_t capacity)
    : _impl(std::make_unique<Impl>(mysql_workers, capacity)) {}
DependencyExecutor::~DependencyExecutor() {
    Stop();
}
void DependencyExecutor::Stop() {
    // 所有停止调用等待同一次线程回收。
    std::call_once(_impl->stop_once, [this] {
        _impl->Stop();
    });
}
LogicSubmitResult DependencyExecutor::SubmitExternal(
    ExternalTask kind,
    Task task,
    RedisAdmission admission,
    SqlGuard guard) {
    if (!task || (guard.check && (kind != ExternalTask::MySQL || !guard.denied)))
        throw std::invalid_argument("invalid dependency task");
    return _impl->Enqueue(kind, {std::move(task), std::move(guard)}, admission);
}
void DependencyExecutor::Drain() {
    std::unique_lock<std::mutex> lock(_impl->mutex);
    // 统计同时涵盖普通工作和预留 SQL。
    _impl->idle.wait(lock, [this] {
        return _impl->outstanding == 0;
    });
}
