#include "LogicDispatcher.h"
#include "Const.h"
#include "CSession.h"

#include <spdlog/spdlog.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

// 短锁保护入口总额度和各片队列；业务处理器和外部完成回调始终在锁外运行。
struct LogicDispatcher::Impl {
    using Session = std::shared_ptr<CSession>;
    // 入站 FIFO 与请求所等待的异步步骤；正在运行的回调仍占用连接。
    struct Mailbox {
        std::queue<LogicMessage> pending;
        bool running = false;
        std::uint64_t waiting_for = 0;
    };
    // 非零 ticket 标记特定连接请求的特定步骤，零表示独立于连接的工作。
    struct Result {
        Session session;
        std::uint64_t ticket;
        Completion next;
    };
    // 每个分片只有一个消费者；ready 中每条有就绪请求的连接只出现一次。
    struct Shard {
        std::condition_variable wake;
        std::map<Session, Mailbox, std::owner_less<Session>> connections;
        std::queue<Session> ready;
        std::queue<Result> completions;
        Session executing;
        std::uint64_t sequence = 0;
        std::thread worker;
    };

    // 全部分片构造后才启动消费者；启动失败也回收已启动的线程。
    Impl(Handler callback, std::shared_ptr<DependencyExecutor> executor, std::size_t count, SessionHash synthetic)
        : handler(std::move(callback)), dependencies(std::move(executor)), synthetic_hash(std::move(synthetic)) {
        if (!count || !dependencies || !handler)
            throw std::invalid_argument("invalid logic dispatcher");
        for (std::size_t i = 0; i < count; ++i)
            shards.push_back(std::make_unique<Shard>());
        try {
            for (auto& shard : shards)
                shard->worker = std::thread(&Impl::Run, this, std::ref(*shard));
        } catch (...) {
            Stop();
            throw;
        }
    }

    // 拒绝非所属 worker 上的请求编排；外部线程只允许调用已经取得的 resume。
    Shard& CallingShard() {
        if (current_dispatcher != this)
            throw std::logic_error("not on owning logic worker");
        return *current_shard;
    }

    // 在锁内登记异步步骤。每次续接生成全片唯一的 ticket，回调按原分片入队。
    Resume Suspend(Shard& shard, Session session, bool ordered) {
        const auto ticket = ordered ? ++shard.sequence : 0;
        // 捕获稳定的分片和步骤，唤醒所属业务线程。
        Resume resume = [this, &shard, session, ticket](Completion next) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                shard.completions.push({session, ticket, std::move(next)});
                shard.wake.notify_one();
            }
        };
        if (ordered) {
            if (session.owner_before(shard.executing) || shard.executing.owner_before(session))
                throw std::logic_error("session does not own current request");
            auto& box = shard.connections.at(session);
            if (!box.running || box.waiting_for)
                throw std::logic_error("request already suspended");
            box.waiting_for = ticket;
        } else {
            ++active;
        }
        return resume;
    }

    // 外部池拒绝时，由当前 handler 处理错误，仍待其返回后才结束整笔请求。
    void CancelSuspension(Shard& shard, const Session& session, bool ordered) {
        if (ordered)
            shard.connections.at(session).waiting_for = 0;
        else
            --active;
    }

    // 入口与在途计数同时归零才允许任何 worker 退出；其他片仍可产生通知。
    bool Drained() const {
        return stopping && pending == 0 && active == 0;
    }

    // 当前步骤或 handler 已返回；仅在没有下一次异步等待时释放原请求。
    void FinishTurn(Shard& shard, const Session& session, bool ordered) {
        std::lock_guard<std::mutex> lock(mutex);
        shard.executing.reset();
        if (ordered) {
            auto it = shard.connections.find(session);
            auto& box = it->second;
            if (box.waiting_for)
                return;
            box.running = false;
            if (box.pending.empty())
                shard.connections.erase(it);
            else
                shard.ready.push(session);
        }
        --active;
        if (Drained())
            WakeAll();
    }

    // 每次只取一条就绪连接或一个完成事件；等待依赖的连接不参与扫描。
    void Run(Shard& shard) {
        current_dispatcher = this;
        current_shard = &shard;
        for (;;) {
            LogicMessage message{};
            Result result{};
            Session session;
            bool initial = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                // 其他分片在停服时也可能唤醒本片收尾。
                shard.wake.wait(lock, [&] {
                    return !shard.completions.empty() || !shard.ready.empty() || Drained();
                });
                if (!shard.completions.empty()) {
                    result = std::move(shard.completions.front());
                    shard.completions.pop();
                    session = result.session;
                    if (result.ticket) {
                        auto it = shard.connections.find(session);
                        if (it == shard.connections.end() || it->second.waiting_for != result.ticket)
                            continue;
                        it->second.waiting_for = 0;
                    }
                } else if (!shard.ready.empty()) {
                    session = std::move(shard.ready.front());
                    shard.ready.pop();
                    auto& box = shard.connections.at(session);
                    message = std::move(box.pending.front());
                    box.pending.pop();
                    box.running = true;
                    --pending;
                    ++active;
                    initial = true;
                } else
                    return;
                shard.executing = session;
            }
            try {
                if (initial) {
                    if (!handler(message))
                        SPDLOG_WARN("logic message handler not found, msg_id={}", message.id);
                } else if (result.next)
                    result.next();
            } catch (...) {
                SPDLOG_ERROR("logic handler or completion failed, msg_id={}", message.id);
            }
            FinishTurn(shard, session, initial || result.ticket != 0);
        }
    }

    // 唤醒所有分片重新检查排空谓词。
    void WakeAll() {
        for (auto& shard : shards)
            shard->wake.notify_one();
    }

    // 停止新输入，所有完成事件仍可投回；由非 worker 所有者调用。
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        WakeAll();
        for (auto& shard : shards)
            if (shard->worker.joinable())
                shard->worker.join();
    }

    inline static thread_local Impl* current_dispatcher = nullptr;
    inline static thread_local Shard* current_shard = nullptr;
    Handler handler;
    std::shared_ptr<DependencyExecutor> dependencies;
    const SessionHash synthetic_hash;
    std::vector<std::unique_ptr<Shard>> shards;
    std::mutex mutex;
    std::size_t pending = 0;
    std::size_t active = 0;
    bool stopping = false;
    std::queue<ExternalWork> notifications;
    bool notifying = false;
    std::uint64_t skipped = 0;
    std::once_flag stop_once;
};

LogicDispatcher::LogicDispatcher(
    Handler handler,
    std::shared_ptr<DependencyExecutor> dependencies,
    std::size_t business_workers)
    : LogicDispatcher(std::move(handler), std::move(dependencies), business_workers, {}) {}

LogicDispatcher::LogicDispatcher(
    Handler handler,
    std::shared_ptr<DependencyExecutor> dependencies,
    std::size_t business_workers,
    SessionHash synthetic_hash)
    : _impl(std::make_unique<Impl>(
          std::move(handler),
          std::move(dependencies),
          business_workers,
          std::move(synthetic_hash))) {}

LogicDispatcher::Resume LogicDispatcher::AwaitCompletion(std::shared_ptr<CSession> session) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->Suspend(_impl->CallingShard(), std::move(session), true);
}

LogicSubmitResult LogicDispatcher::SubmitExternal(
    ExternalTask kind,
    std::shared_ptr<CSession> session,
    ExternalWork work,
    Completion failed,
    std::function<bool()> sql_check) {
    const bool ordered = session.use_count() != 0;
    auto& shard = _impl->CallingShard();
    Resume resume;
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        resume = _impl->Suspend(shard, session, ordered);
    }
    LogicSubmitResult result;
    try {
        DependencyExecutor::SqlGuard guard;
        if (sql_check) {
            guard.check = std::move(sql_check);
            // 复核未通过或者无法入队，依旧完成原请求。
            guard.denied = [resume, failed] {
                resume(failed);
            };
        }
        // 所有后续业务都在分发线程恢复。
        result = _impl->dependencies->SubmitExternal(
            kind,
            [resume, work = std::move(work), failed]() mutable {
                Completion next;
                try {
                    next = work();
                } catch (...) {
                    next = failed;
                    SPDLOG_ERROR("external request failed");
                }
                resume(std::move(next));
            },
            RedisAdmission::Critical,
            std::move(guard));
    } catch (...) {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        _impl->CancelSuspension(shard, session, ordered);
        throw;
    }
    if (result != LogicSubmitResult::Accepted) {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        _impl->CancelSuspension(shard, session, ordered);
    }
    return result;
}

void LogicDispatcher::QueueNotification(ExternalWork route) {
    {
        std::unique_lock<std::mutex> lock(_impl->mutex);
        if (_impl->notifying) {
            if (_impl->notifications.size() < 64) {
                _impl->notifications.push(std::move(route));
            } else {
                const auto count = ++_impl->skipped;
                lock.unlock();
                if ((count & (count - 1)) == 0)
                    SPDLOG_WARN("notification backlog full, skipped={}", count);
            }
            return;
        }
        _impl->notifying = true;
    }
    StartNotification(std::move(route));
}

void LogicDispatcher::StartNotification(ExternalWork route) {
    Resume resume;
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        resume = _impl->Suspend(*_impl->shards.front(), {}, false);
    }
    // 队列由进程共享，通知完成统一回到第一个分片；持锁时只操作 FIFO。
    auto next = [this] {
        ExternalWork route;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            if (_impl->notifications.empty()) {
                _impl->notifying = false;
                return;
            }
            route = std::move(_impl->notifications.front());
            _impl->notifications.pop();
        }
        StartNotification(std::move(route));
    };
    // 路由完成后将远端调用交给 gRPC 池，本机发送已由会话 strand 接管。
    auto resolve = [this, route = std::move(route), resume, next]() mutable {
        try {
            auto rpc = route();
            if (rpc) {
                // 调用结束后释放逻辑通知位置。
                auto send = [rpc = std::move(rpc), resume, next] {
                    try {
                        rpc();
                    } catch (...) {
                        SPDLOG_WARN("notification RPC failed");
                    }
                    resume(next);
                };
                if (_impl->dependencies->SubmitExternal(ExternalTask::OutboundGrpc, std::move(send)) ==
                    LogicSubmitResult::Accepted)
                    return;
            }
        } catch (...) {
            SPDLOG_WARN("notification routing failed");
        }
        resume(next);
    };
    if (_impl->dependencies->SubmitExternal(ExternalTask::Redis, std::move(resolve), RedisAdmission::Routing) !=
        LogicSubmitResult::Accepted)
        resume(next);
}

LogicDispatcher::~LogicDispatcher() {
    Stop();
}

LogicSubmitResult LogicDispatcher::Submit(LogicMessage message) {
    std::size_t hash = 0;
    if (message.session)
        hash = message.session->DispatchHash();
    else if (_impl->synthetic_hash)
        hash = _impl->synthetic_hash(message.session);
    auto& shard = *_impl->shards[hash % _impl->shards.size()];
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (_impl->stopping)
            return LogicSubmitResult::Closed;
        if (_impl->pending >= MAX_DEALQUE)
            return LogicSubmitResult::Full;
        auto [it, inserted] = shard.connections.try_emplace(message.session);
        try {
            it->second.pending.push(std::move(message));
            if (inserted)
                shard.ready.push(it->first);
        } catch (...) {
            if (inserted)
                shard.connections.erase(it);
            throw;
        }
        ++_impl->pending;
        shard.wake.notify_one();
    }
    return LogicSubmitResult::Accepted;
}

void LogicDispatcher::Stop() {
    // 共享一次关闭并等待全部业务线程退出。
    std::call_once(_impl->stop_once, [this] {
        _impl->Stop();
    });
}
