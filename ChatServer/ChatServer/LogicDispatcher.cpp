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
    // 启动消费者；构造失败也回收已启动的数据库线程。
    Impl(Handler callback, std::size_t count, std::size_t capacity)
        : handler(std::move(callback)), database_capacity(capacity) {
        if (!count || !capacity)
            throw std::invalid_argument("invalid database executor capacity");
        try {
            for (std::size_t i = 0; i < count; ++i)
                // 数据库消费者在各自线程运行。
                database_workers.emplace_back([this] {
                    RunDatabase();
                });
            // 一个分发线程统一处理入口和完成回调。
            worker = std::thread([this] {
                Run();
            });
        } catch (...) {
            StopDatabase();
            throw;
        }
    }

    // 锁内以连接所有权登记在途操作；通知执行前不会复用该连接的身份。
    Resume Suspend(std::shared_ptr<CSession> session) {
        // 异步生产者仅交回后续操作，分发线程取走后才解除占用。
        Resume resume = [this, session](Completion next) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                completions.emplace(session, std::move(next));
            }
            condition.notify_one();
        };
        if (!busy.insert(session).second)
            throw std::logic_error("session already suspended");
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
                    if (!completions.empty() || (stopping && messages.empty() && busy.empty()))
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
                    busy.erase(item.first);
                    completion = std::move(item.second);
                    if (!completion)
                        continue;
                } else if (ready != messages.end()) {
                    message = std::move(*ready);
                    messages.erase(ready);
                } else {
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

    // 数据库工作线程在锁外执行同步操作。
    void RunDatabase() {
        for (;;) {
            Completion task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                // 等待新数据库任务或停机信号。
                database_ready.wait(lock, [this] {
                    return database_stopping || !database.empty();
                });
                if (database.empty())
                    return;
                task = std::move(database.front());
                database.pop();
            }
            task();
        }
    }

    // 在所有在途操作交回结果后回收数据库线程。
    void StopDatabase() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            database_stopping = true;
        }
        database_ready.notify_all();
        for (auto& thread : database_workers)
            thread.join();
    }

    Handler handler;
    const std::size_t database_capacity;
    std::mutex mutex;
    std::condition_variable condition, database_ready;
    std::list<LogicMessage> messages;
    std::set<std::shared_ptr<CSession>, std::owner_less<std::shared_ptr<CSession>>> busy;
    std::queue<std::pair<std::shared_ptr<CSession>, Completion>> completions;
    std::queue<Completion> database;
    bool stopping{false};
    bool database_stopping{false};
    std::once_flag stop_once;
    std::vector<std::thread> database_workers;
    std::thread worker;
};

LogicDispatcher::LogicDispatcher(Handler handler, std::size_t database_workers, std::size_t database_capacity)
    : _impl(std::make_unique<Impl>(std::move(handler), database_workers, database_capacity)) {}

LogicDispatcher::Resume LogicDispatcher::AwaitCompletion(std::shared_ptr<CSession> session) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->Suspend(std::move(session));
}

LogicSubmitResult LogicDispatcher::SubmitDatabase(
    std::shared_ptr<CSession> session,
    DatabaseWork work,
    Completion failed) {
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (_impl->database_stopping)
            return LogicSubmitResult::Closed;
        if (_impl->database.size() >= _impl->database_capacity)
            return LogicSubmitResult::Full;
        auto resume = _impl->Suspend(session);
        try {
            // 异常和正常结果共用完成通道。
            _impl->database.emplace([work = std::move(work), failed = std::move(failed), resume = std::move(resume)] {
                Completion next;
                try {
                    next = work();
                } catch (...) {
                    SPDLOG_ERROR("database task failed");
                    next = failed;
                }
                resume(std::move(next));
            });
        } catch (...) {
            _impl->busy.erase(session);
            throw;
        }
    }
    _impl->database_ready.notify_one();
    return LogicSubmitResult::Accepted;
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
            _impl->StopDatabase();
        }
    });
}
