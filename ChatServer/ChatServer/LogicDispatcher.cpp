#include "LogicDispatcher.h"
#include "Const.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <condition_variable>
#include <list>
#include <set>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

/** @brief 拥有外层服务的运行状态和异步操作所需资源，生命周期由外层实现约束。 */
struct LogicDispatcher::Impl {
    // 已完成的通知不关联发送连接；请求完成时才释放会话的顺序位置。
    struct Result {
        std::shared_ptr<CSession> session;
        Completion next;
        bool ordered;
    };

    // 分发线程只负责请求编排，依赖池由进程共享。
    Impl(Handler callback, std::shared_ptr<DependencyExecutor> executor)
        : handler(std::move(callback)), dependencies(std::move(executor)), worker(&Impl::Run, this) {}

    // 锁内以连接所有权登记在途操作；通知执行前不会复用该连接的身份。
    Resume Suspend(std::shared_ptr<CSession> session, bool ordered = true) {
        // 异步生产者仅交回后续操作，分发线程取走后才解除占用。
        Resume resume = [this, session, ordered](Completion next) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                completions.push({session, std::move(next), ordered});
            }
            condition.notify_one();
        };
        if (ordered && !busy.insert(session).second)
            throw std::logic_error("session already suspended");
        ++in_flight;
        return resume;
    }

    // 跳过有在途任务的连接，其余消息保持入口顺序，所有暂存项计入同一容量。
    void Run() {
        for (;;) {
            LogicMessage message{};
            Completion completion;
            {
                std::unique_lock<std::mutex> lock(mutex);
                auto ready = messages.end();
                // 完成事件优先，入口关闭后仍等待在途操作。
                condition.wait(lock, [&] {
                    if (!completions.empty() || (stopping && messages.empty() && in_flight == 0))
                        return true;
                    // 选择未被在途操作占用的连接。
                    ready = std::find_if(messages.begin(), messages.end(), [&](const LogicMessage& item) {
                        return !busy.count(item.session);
                    });
                    return ready != messages.end();
                });
                if (!completions.empty()) {
                    auto item = std::move(completions.front());
                    completions.pop();
                    if (item.ordered)
                        busy.erase(item.session);
                    --in_flight;
                    completion = std::move(item.next);
                    if (!completion)
                        continue;
                } else if (ready != messages.end()) {
                    message = std::move(*ready);
                    messages.erase(ready);
                } else {
                    drained = true;
                    return;
                }
            }
            try {
                if (completion)
                    completion();
                else if (!handler(message))
                    SPDLOG_WARN("logic message handler not found, msg_id={}", message.id);
            } catch (...) {
                SPDLOG_ERROR("logic message handler failed, msg_id={}", message.id);
            }
        }
    }

    Handler handler;
    std::shared_ptr<DependencyExecutor> dependencies;
    std::queue<ExternalWork> notifications;
    bool notifying = false;
    std::uint64_t skipped = 0;
    std::mutex mutex;
    std::condition_variable condition;
    std::list<LogicMessage> messages;
    std::set<std::shared_ptr<CSession>, std::owner_less<std::shared_ptr<CSession>>> busy;
    std::queue<Result> completions;
    std::size_t in_flight = 0;
    bool stopping{false};
    bool drained{false};

    std::once_flag stop_once;
    std::thread worker;
};

LogicDispatcher::LogicDispatcher(Handler handler, std::size_t database_workers, std::size_t capacity_per_pool)
    : LogicDispatcher(std::move(handler), std::make_shared<DependencyExecutor>(database_workers, capacity_per_pool)) {}

LogicDispatcher::LogicDispatcher(Handler handler, std::shared_ptr<DependencyExecutor> dependencies)
    : _impl(std::make_unique<Impl>(std::move(handler), std::move(dependencies))) {}

LogicDispatcher::Resume LogicDispatcher::AwaitCompletion(std::shared_ptr<CSession> session) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->Suspend(std::move(session));
}

LogicSubmitResult LogicDispatcher::SubmitExternal(
    ExternalTask kind,
    std::shared_ptr<CSession> session,
    ExternalWork work,
    Completion failed,
    std::function<bool()> sql_check) {
    const bool ordered = session.use_count() != 0;
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->drained)
        return LogicSubmitResult::Closed;
    auto resume = _impl->Suspend(session, ordered);
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
        if (ordered)
            _impl->busy.erase(session);
        --_impl->in_flight;
        throw;
    }
    if (result != LogicSubmitResult::Accepted) {
        if (ordered)
            _impl->busy.erase(session);
        --_impl->in_flight;
    }
    return result;
}

void LogicDispatcher::QueueNotification(ExternalWork route) {
    if (!_impl->notifying) {
        _impl->notifying = true;
        StartNotification(std::move(route));
    } else if (_impl->notifications.size() < 64) {
        _impl->notifications.push(std::move(route));
    } else {
        const auto count = ++_impl->skipped;
        if ((count & (count - 1)) == 0)
            SPDLOG_WARN("notification backlog full, skipped={}", count);
    }
}

void LogicDispatcher::StartNotification(ExternalWork route) {
    Resume resume;
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        resume = _impl->Suspend({}, false);
    }
    // 仅在分发线程访问通知 FIFO，发送者断开不会取消已提交的通知。
    auto next = [this] {
        if (_impl->notifications.empty()) {
            _impl->notifying = false;
            return;
        }
        auto route = std::move(_impl->notifications.front());
        _impl->notifications.pop();
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
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (_impl->stopping) {
            return LogicSubmitResult::Closed;
        }
        if (_impl->messages.size() >= MAX_DEALQUE) {
            return LogicSubmitResult::Full;
        }
        _impl->messages.push_back(std::move(message));
    }
    _impl->condition.notify_one();
    return LogicSubmitResult::Accepted;
}

void LogicDispatcher::Stop() {
    // 一次性标记停止、唤醒消费者并等待工作线程结束。
    std::call_once(_impl->stop_once, [this]() {
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            _impl->stopping = true;
        }
        _impl->condition.notify_one();
        if (_impl->worker.joinable()) {
            _impl->worker.join();
        }
    });
}
