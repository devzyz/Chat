#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "LogicDispatcher.h"
#include "InstanceLease.h"
#include "Const.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

/** 作用域内捕获默认日志输出，用于验证正文和异常不泄露。 */
class ScopedLogCapture {
public:
    /** 安装内存日志器并保存原默认日志器。 */
    ScopedLogCapture()
        : previous_(spdlog::default_logger()), sink_(std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_)),
          logger_(std::make_shared<spdlog::logger>("logic-dispatcher-test", sink_)) {
        logger_->set_pattern("%v");
        logger_->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger_);
    }

    /** 恢复原默认日志器。 */
    ~ScopedLogCapture() {
        spdlog::set_default_logger(previous_);
    }

    /** 刷新并返回捕获日志文本。 */
    std::string Text() {
        logger_->flush();
        return stream_.str();
    }

private:
    std::shared_ptr<spdlog::logger> previous_;
    std::ostringstream stream_;
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink_;
    std::shared_ptr<spdlog::logger> logger_;
};

// 为不依赖 socket 的调度测试创建单独的连接所有权。
std::shared_ptr<CSession> SessionKey() {
    return {std::make_shared<int>(0), nullptr};
}

// T08-LOGIC-01
/** 验证被接受消息按 FIFO 顺序委派。 */
TEST(LogicDispatcherTests, SameConnectionDispatchesInFifoOrder) {
    std::mutex mutex;
    std::condition_variable dispatched;
    std::vector<std::int16_t> ids;
    // 记录消息编号并唤醒等待者。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ids.push_back(message.id);
        }
        dispatched.notify_all();
        return true;
    });

    const auto connection = SessionKey();
    EXPECT_EQ(dispatcher.Submit({connection, 11, "first", 0, 1}), LogicSubmitResult::Accepted);
    EXPECT_EQ(dispatcher.Submit({connection, 12, "second", 0, 1}), LogicSubmitResult::Accepted);
    EXPECT_EQ(dispatcher.Submit({connection, 13, "third", 0, 1}), LogicSubmitResult::Accepted);

    {
        std::unique_lock<std::mutex> lock(mutex);
        // 判断三条消息是否全部处理。
        ASSERT_TRUE(dispatched.wait_for(lock, 2s, [&]() {
            return ids.size() == 3;
        }));
    }
    dispatcher.Stop();
    EXPECT_EQ(ids, (std::vector<std::int16_t>{11, 12, 13}));
}

// T08-LOGIC-02
/** 验证并发生产者的每条已接受消息恰好处理一次。 */
TEST(LogicDispatcherTests, ConcurrentProducersDispatchEveryAcceptedMessageExactlyOnce) {
    constexpr int producer_count = 8;
    constexpr int messages_per_producer = 50;
    constexpr int message_count = producer_count * messages_per_producer;
    std::mutex mutex;
    std::condition_variable condition;
    bool start = false;
    std::vector<int> dispatch_counts(message_count, 0);
    int dispatched_count = 0;
    std::vector<LogicSubmitResult> results(message_count, LogicSubmitResult::Closed);
    // 在锁内累计各消息处理次数并通知等待者。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++dispatch_counts[static_cast<std::size_t>(std::stoi(message.body))];
            ++dispatched_count;
        }
        condition.notify_all();
        return true;
    });

    std::vector<std::thread> producers;
    for (int producer = 0; producer < producer_count; ++producer) {
        // 等待起跑屏障后提交本生产者的独立消息区间。
        producers.emplace_back([&, producer]() {
            {
                std::unique_lock<std::mutex> lock(mutex);
                // 检查并发生产者起跑屏障。
                condition.wait(lock, [&]() {
                    return start;
                });
            }
            for (int index = 0; index < messages_per_producer; ++index) {
                const int value = producer * messages_per_producer + index;
                results[static_cast<std::size_t>(value)] =
                    dispatcher.Submit({nullptr, static_cast<std::int16_t>(value), std::to_string(value)});
            }
        });
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        start = true;
    }
    condition.notify_all();
    for (auto& producer : producers) {
        producer.join();
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        // 判断全部消息是否处理完成。
        ASSERT_TRUE(condition.wait_for(lock, 2s, [&]() {
            return dispatched_count == message_count;
        }));
    }
    dispatcher.Stop();
    EXPECT_EQ(std::count(results.begin(), results.end(), LogicSubmitResult::Accepted), message_count);
    // 检查单条消息恰好处理一次。
    EXPECT_TRUE(std::all_of(dispatch_counts.begin(), dispatch_counts.end(), [](int count) {
        return count == 1;
    }));
}

// T08-LOGIC-03：同时占住两个分片后，两个连接合计只能排 MAX_DEALQUE 笔。
TEST(LogicDispatcherTests, ShardsShareOneExactAdmissionLimit) {
    std::promise<void> first, second, release;
    auto gate = release.get_future().share();
    std::thread::id workers[2];
    int counts[2]{};
    const auto left = SessionKey(), right = SessionKey();
    // 屏障内两个 handler 必须同时运行，释放后分别检查连接 FIFO。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        auto shard = message.connection_hash;
        if (message.id == 1) {
            workers[shard] = std::this_thread::get_id();
            (shard ? second : first).set_value();
            EXPECT_EQ(gate.wait_for(3s), std::future_status::ready);
        } else {
            EXPECT_EQ(workers[shard], std::this_thread::get_id());
            EXPECT_EQ(counts[shard]++, std::stoi(message.body));
        }
        return true;
    });
    dispatcher.Submit({left, 1, {}, 0, 0});
    dispatcher.Submit({right, 1, {}, 0, 1});
    ASSERT_EQ(first.get_future().wait_for(2s), std::future_status::ready);
    ASSERT_EQ(second.get_future().wait_for(2s), std::future_status::ready);
    EXPECT_NE(workers[0], workers[1]);
    for (int i = 0; i < MAX_DEALQUE; ++i) {
        const std::size_t key = i % 2;
        ASSERT_EQ(
            dispatcher.Submit({key ? right : left, 2, std::to_string(i / 2), 0, key}),
            LogicSubmitResult::Accepted);
    }
    EXPECT_EQ(dispatcher.Submit({right, 2, {}, 0, 1}), LogicSubmitResult::Full);
    release.set_value();
    dispatcher.Stop();
    EXPECT_EQ(counts[0] + counts[1], MAX_DEALQUE);
}

// T08-LOGIC-04
/** 验证空闲工作线程会被消息或停止唤醒。 */
TEST(LogicDispatcherTests, WaitingWorkerWakesForMessageAndStop) {
    std::promise<void> handled_promise;
    auto handled = handled_promise.get_future();
    // 通知消息处理已完成。
    LogicDispatcher message_dispatcher([&](const LogicMessage&) {
        handled_promise.set_value();
        return true;
    });
    ASSERT_EQ(message_dispatcher.Submit({nullptr, 20, "wake"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(handled.wait_for(2s), std::future_status::ready);
    message_dispatcher.Stop();

    // 提供正常空操作处理器以验证空闲停机。
    LogicDispatcher idle_dispatcher([](const LogicMessage&) {
        return true;
    });
    // 在异步任务中停止空闲分发器。
    auto stopped = std::async(std::launch::async, [&]() {
        idle_dispatcher.Stop();
    });
    ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
    EXPECT_NO_THROW(stopped.get());
}

// T08-LOGIC-05
/** 验证停止在两秒内排空已接受消息。 */
TEST(LogicDispatcherTests, StopDrainsAcceptedMessagesWithinTwoSeconds) {
    constexpr int message_count = 100;
    std::mutex mutex;
    std::condition_variable condition;
    bool first_handler_started = false;
    bool release_handler = false;
    int handled_count = 0;
    // 累计已处理消息并在首条建立可控屏障。
    LogicDispatcher dispatcher([&](const LogicMessage&) {
        std::unique_lock<std::mutex> lock(mutex);
        ++handled_count;
        if (!first_handler_started) {
            first_handler_started = true;
            condition.notify_all();
            // 判断首条处理阻塞是否已释放。
            condition.wait(lock, [&]() {
                return release_handler;
            });
        }
        return true;
    });

    for (int index = 0; index < message_count; ++index) {
        ASSERT_EQ(dispatcher.Submit({nullptr, 30, std::to_string(index)}), LogicSubmitResult::Accepted);
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        // 判断首条处理是否已进入。
        ASSERT_TRUE(condition.wait_for(lock, 2s, [&]() {
            return first_handler_started;
        }));
    }
    // 在异步任务中执行排空停机。
    auto stopped = std::async(std::launch::async, [&]() {
        dispatcher.Stop();
    });
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_handler = true;
    }
    condition.notify_all();
    ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
    EXPECT_NO_THROW(stopped.get());
    EXPECT_EQ(handled_count, message_count);
}

// T08-LOGIC-06
/** 验证关闭后提交立即拒绝且重复停止幂等。 */
TEST(LogicDispatcherTests, ClosedDispatcherRejectsImmediatelyAndRepeatedStopIsIdempotent) {
    std::atomic<int> handled_count{0};
    // 累计被调用次数以检测关闭后误处理。
    LogicDispatcher dispatcher([&](const LogicMessage&) {
        handled_count.fetch_add(1);
        return true;
    });

    dispatcher.Stop();
    EXPECT_NO_THROW(dispatcher.Stop());
    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(dispatcher.Submit({nullptr, 40, "closed"}), LogicSubmitResult::Closed);
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
    EXPECT_EQ(handled_count.load(), 0);
}

// T08-LOGIC-07
/** 验证未知消息编号不阻塞后续合法消息且日志省略正文。 */
TEST(LogicDispatcherTests, UnknownIdDoesNotBlockValidMessageAndLogOmitsBody) {
    ScopedLogCapture logs;
    std::promise<void> valid_handled_promise;
    auto valid_handled = valid_handled_promise.get_future();
    // 只接受指定有效编号并通知处理完成。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        if (message.id != 77) {
            return false;
        }
        valid_handled_promise.set_value();
        return true;
    });

    ASSERT_EQ(dispatcher.Submit({nullptr, 999, "SYNTHETIC_BODY_MARKER_3A01"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(dispatcher.Submit({nullptr, 77, "valid"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(valid_handled.wait_for(2s), std::future_status::ready);
    dispatcher.Stop();

    const auto text = logs.Text();
    EXPECT_NE(text.find("999"), std::string::npos);
    EXPECT_EQ(text.find("SYNTHETIC_BODY_MARKER_3A01"), std::string::npos);
}

// T08-LOGIC-08
/** 验证处理器异常不终止工作线程且日志不暴露请求或异常正文。 */
TEST(LogicDispatcherTests, ThrowingHandlerDoesNotTerminateWorkerOrExposePayload) {
    ScopedLogCapture logs;
    std::promise<void> handled;
    auto ready = handled.get_future();
    // 对指定编号抛含敏感标记的异常，其余消息正常完成。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        if (message.id == 90)
            throw std::runtime_error("PRIVATE_EXCEPTION_PAYLOAD");
        handled.set_value();
        return true;
    });
    ASSERT_EQ(dispatcher.Submit({nullptr, 90, "PRIVATE_REQUEST_PAYLOAD"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(dispatcher.Submit({nullptr, 91, "valid"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
    dispatcher.Stop();
    EXPECT_EQ(logs.Text().find("PRIVATE_"), std::string::npos);
}

// T08-LOGIC-09：暂停两笔数据库工作时，第三个连接可以处理心跳，同连接读取必须在写完成后运行。
TEST(LogicDispatcherTests, SlowWritesAllowOtherSessionsAndPreserveReadAfterWrite) {
    std::promise<void> first, second, heartbeat, release;
    auto gate = release.get_future().share();
    bool committed = false;
    bool observed = false;
    const auto writer = SessionKey();
    LogicDispatcher* executor = nullptr;
    // 记录心跳、写入和后续读取的处理顺序。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        if (message.id == 3) {
            heartbeat.set_value();
        } else if (message.id == 4) {
            observed = committed;
        } else {
            const auto id = message.id;
            const auto thread = std::this_thread::get_id();
            // 工作线程暂停到测试释放门闩；回调必须回到业务线程。
            EXPECT_EQ(
                executor->SubmitExternal(
                    ExternalTask::MySQL,
                    message.session,
                    [&, id, thread]() -> LogicDispatcher::Completion {
                        EXPECT_NE(thread, std::this_thread::get_id());
                        (id == 1 ? first : second).set_value();
                        EXPECT_EQ(gate.wait_for(2s), std::future_status::ready);
                        // 检查写入完成回到分发线程。
                        return [&, id, thread] {
                            EXPECT_EQ(thread, std::this_thread::get_id());
                            if (id == 1)
                                committed = true;
                        };
                    },
                    {}),
                LogicSubmitResult::Accepted);
        }
        return true;
    });
    executor = &dispatcher;
    dispatcher.Submit({writer, 1, {}});
    dispatcher.Submit({writer, 4, {}});
    dispatcher.Submit({SessionKey(), 2, {}});
    dispatcher.Submit({SessionKey(), 3, {}});
    EXPECT_EQ(first.get_future().wait_for(2s), std::future_status::ready);
    EXPECT_EQ(second.get_future().wait_for(2s), std::future_status::ready);
    EXPECT_EQ(heartbeat.get_future().wait_for(2s), std::future_status::ready);
    release.set_value();
    dispatcher.Stop();
    EXPECT_TRUE(observed);
}

// T08-LOGIC-10：一个在执行、一个在排队时拒绝第三笔；停服仍交回异常并排空已受理任务。
TEST(LogicDispatcherTests, DatabaseCapacityRejectsWithoutWritingAndDrainsFailures) {
    std::promise<void> started, release, full;
    auto gate = release.get_future().share();
    int writes = 0;
    int failures = 0;
    LogicDispatcher* executor = nullptr;
    // 记录满载时的受理结果并检查异常回传。
    LogicDispatcher dispatcher(
        [&](const LogicMessage& message) {
            const auto id = message.id;
            // 第一笔故意抛错，第二笔应正常执行，第三笔不应进入数据库。
            auto result = executor->SubmitExternal(
                ExternalTask::MySQL,
                message.session,
                [&, id]() -> LogicDispatcher::Completion {
                    if (id == 1) {
                        started.set_value();
                        EXPECT_EQ(gate.wait_for(2s), std::future_status::ready);
                        throw std::runtime_error("synthetic failure");
                    }
                    ++writes;
                    return {};
                },
                [&] {
                    ++failures;
                });
            EXPECT_EQ(result, id == 3 ? LogicSubmitResult::Full : LogicSubmitResult::Accepted);
            if (id == 3)
                full.set_value();
            return true;
        },
        std::make_shared<DependencyExecutor>(1, 1),
        1);
    executor = &dispatcher;
    dispatcher.Submit({SessionKey(), 1, {}});
    ASSERT_EQ(started.get_future().wait_for(2s), std::future_status::ready);
    dispatcher.Submit({SessionKey(), 2, {}});
    dispatcher.Submit({SessionKey(), 3, {}});
    EXPECT_EQ(full.get_future().wait_for(2s), std::future_status::ready);
    release.set_value();
    dispatcher.Stop();
    EXPECT_EQ(writes, 1);
    EXPECT_EQ(failures, 1);
}

// T08-LOGIC-11：Redis 复核与出站 gRPC 独立；RPC 队满时仍可处理其他连接的 Redis 和 SQL 步骤。
TEST(LogicDispatcherTests, SlowRedisAndGrpcAllowIndependentSqlAndDrain) {
    std::promise<void> auth_started, notification_started, second_auth, release, submitted;
    auto gate = release.get_future().share();
    std::vector<int> delivered;
    LogicDispatcher* executor = nullptr;
    // 在两个不同的池内建立屏障，另一个认证及有序后续数据库步骤仍能完成。
    LogicDispatcher dispatcher(
        [&](const LogicMessage& msg) {
            if (msg.id == 1) {
                // 暂停一笔认证，验证另一条连接能使用第二个 Redis worker。
                executor->SubmitExternal(
                    ExternalTask::Redis,
                    msg.session,
                    [&]() -> LogicDispatcher::Completion {
                        auth_started.set_value();
                        EXPECT_EQ(gate.wait_for(2s), std::future_status::ready);
                        return {};
                    },
                    {});
            } else if (msg.id == 2) {
                // 模拟对端 RPC 未返回。
                executor->SubmitExternal(
                    ExternalTask::OutboundGrpc,
                    {},
                    [&]() -> LogicDispatcher::Completion {
                        notification_started.set_value();
                        EXPECT_EQ(gate.wait_for(2s), std::future_status::ready);
                        delivered.push_back(1);
                        return {};
                    },
                    {});
            } else {
                for (int index : {2, 3}) {
                    // 满队列拒绝的第三笔不能执行。
                    auto status = executor->SubmitExternal(
                        ExternalTask::OutboundGrpc,
                        {},
                        [&, index]() -> LogicDispatcher::Completion {
                            delivered.push_back(index);
                            return {};
                        },
                        {});
                    EXPECT_EQ(status, index == 2 ? LogicSubmitResult::Accepted : LogicSubmitResult::Full);
                }
                // 从认证完成事件继续查询；不占用被阻塞的出站 gRPC worker。
                executor->SubmitExternal(
                    ExternalTask::Redis,
                    msg.session,
                    [&, session = msg.session]() -> LogicDispatcher::Completion {
                        // 回到分发线程后再提交同一连接的数据库操作。
                        return [&, session] {
                            // 查询开始即证明通知和慢认证没有耗尽数据库容量。
                            executor->SubmitExternal(
                                ExternalTask::MySQL,
                                session,
                                [&]() -> LogicDispatcher::Completion {
                                    second_auth.set_value();
                                    return {};
                                },
                                {});
                        };
                    },
                    {});
                submitted.set_value();
            }
            return true;
        },
        std::make_shared<DependencyExecutor>(1, 1),
        1);
    executor = &dispatcher;
    dispatcher.Submit({SessionKey(), 1, {}});
    dispatcher.Submit({SessionKey(), 2, {}});
    EXPECT_EQ(auth_started.get_future().wait_for(2s), std::future_status::ready);
    EXPECT_EQ(notification_started.get_future().wait_for(2s), std::future_status::ready);
    dispatcher.Submit({SessionKey(), 3, {}});
    EXPECT_EQ(submitted.get_future().wait_for(2s), std::future_status::ready);
    EXPECT_EQ(second_auth.get_future().wait_for(2s), std::future_status::ready);
    // 入口关闭后，通知仍需按入队顺序排空。
    auto stopped = std::async(std::launch::async, [&] {
        dispatcher.Stop();
    });
    release.set_value();
    EXPECT_EQ(stopped.wait_for(2s), std::future_status::ready);
    stopped.get();
    EXPECT_EQ(delivered, (std::vector<int>{1, 2}));
}

// T08-LOGIC-12：阻塞两个 Redis worker，56 笔普通路由之后仍能接纳 8 笔关键任务。
TEST(LogicDispatcherTests, RedisReservesCriticalCapacityAndRejectsGuardedWritesWhenFull) {
    std::promise<void> first, second, release, denied;
    auto gate = release.get_future().share();
    // 每个屏障由一个不同的 Redis 工作者占用。
    auto block = [&](std::promise<void>& entered) {
        EXPECT_TRUE(DependencyExecutor::RunningOn(ExternalTask::Redis));
        entered.set_value();
        EXPECT_EQ(gate.wait_for(2s), std::future_status::ready);
    };
    DependencyExecutor executor;
    executor.SubmitExternal(ExternalTask::Redis, std::bind(block, std::ref(first)));
    executor.SubmitExternal(ExternalTask::Redis, std::bind(block, std::ref(second)));
    ASSERT_EQ(first.get_future().wait_for(2s), std::future_status::ready);
    ASSERT_EQ(second.get_future().wait_for(2s), std::future_status::ready);
    // 普通路由与关键任务共用队列，仅准入配额不同。
    auto noop = [] {
    };
    for (int n = 0; n < 56; ++n)
        EXPECT_EQ(
            executor.SubmitExternal(ExternalTask::Redis, noop, RedisAdmission::Routing),
            LogicSubmitResult::Accepted);
    EXPECT_EQ(executor.SubmitExternal(ExternalTask::Redis, noop, RedisAdmission::Routing), LogicSubmitResult::Full);
    for (int n = 0; n < 8; ++n)
        EXPECT_EQ(executor.SubmitExternal(ExternalTask::Redis, noop), LogicSubmitResult::Accepted);
    // 复核连入队都失败时不能执行 SQL，拒绝回调仍完成。
    DependencyExecutor::SqlGuard guard;
    // 若资源隔离错误而进入复核，该检查会成功。
    guard.check = [] {
        return true;
    };
    // 让等待者确认这笔写入已被拒绝。
    guard.denied = [&] {
        denied.set_value();
    };
    // 被拒绝的任务不可产生数据库副作用。
    auto sql = [] {
        ADD_FAILURE() << "unexpected SQL";
    };
    EXPECT_EQ(
        executor.SubmitExternal(ExternalTask::MySQL, sql, RedisAdmission::Critical, guard),
        LogicSubmitResult::Accepted);
    EXPECT_EQ(denied.get_future().wait_for(2s), std::future_status::ready);
    release.set_value();
    executor.Stop();
    EXPECT_EQ(executor.SubmitExternal(ExternalTask::Redis, noop), LogicSubmitResult::Closed);
}

// T08-LOGIC-13：轮到执行后才复核，等待中的 SQL 不得越过已预留名额；停服仍排空后续操作。
TEST(LogicDispatcherTests, ReservedSqlResumesAheadOfLaterWorkDuringDrain) {
    std::promise<void> checking, release;
    auto gate = release.get_future().share();
    std::vector<int> order;
    // 复核在 Redis 上执行，MySQL 上不持有等待 Redis 的调用栈。
    DependencyExecutor::SqlGuard guard;
    // 记录复核实际所属线程并控制放行顺序。
    guard.check = [&] {
        EXPECT_TRUE(DependencyExecutor::RunningOn(ExternalTask::Redis));
        checking.set_value();
        return gate.wait_for(2s) == std::future_status::ready;
    };
    // 正常放行后不应触发拒绝回调。
    guard.denied = [] {
        ADD_FAILURE() << "lost SQL reservation";
    };
    // 检查结果必须返回 MySQL 线程执行写入。
    auto write = [&] {
        EXPECT_TRUE(DependencyExecutor::RunningOn(ExternalTask::MySQL));
        order.push_back(1);
    };
    DependencyExecutor executor(1, 4);
    executor.SubmitExternal(ExternalTask::MySQL, write, RedisAdmission::Critical, guard);
    ASSERT_EQ(checking.get_future().wait_for(2s), std::future_status::ready);
    // 这笔读取应排在预留写入之后。
    executor.SubmitExternal(ExternalTask::MySQL, [&] {
        order.push_back(2);
    });
    // 从所有者线程验证带预留名额的停服。
    auto stopped = std::async(std::launch::async, [&] {
        executor.Stop();
    });
    // 等到外部入口可观察地关闭，再释放预留名额，避免 Stop 尚未运行就通过测试。
    auto probe = [] {
    };
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    auto admission = LogicSubmitResult::Accepted;
    while (std::chrono::steady_clock::now() < deadline) {
        admission = executor.SubmitExternal(ExternalTask::OutboundGrpc, probe);
        if (admission == LogicSubmitResult::Closed)
            break;
        stopped.wait_for(1ms);
    }
    EXPECT_EQ(admission, LogicSubmitResult::Closed);
    release.set_value();
    ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
    stopped.get();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

// T08-LOGIC-14：维护适配器记录执行线程、所有者与顺序，排队续租不能在注销后复活实例。
TEST(LogicDispatcherTests, InstanceLeaseUsesRedisAndUnregistersAfterRenewal) {
    std::vector<std::string> actions;
    std::mutex actions_mutex;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    DependencyExecutor executor;
    // 登记、续租和注销使用同一 owner；记录 Redis 返回值及操作次序。
    InstanceLease lease(
        executor,
        [&](const auto& script, const auto& keys, const auto& args) -> std::optional<InstanceLease::Values> {
            EXPECT_TRUE(DependencyExecutor::RunningOn(ExternalTask::Redis));
            EXPECT_EQ(keys.back(), "chatlease_test");
            if (script.find("registered") != std::string::npos) {
                EXPECT_EQ(args[1], "endpoint|nonce");
                actions.push_back("registered");
                return InstanceLease::Values{"registered"};
            }
            if (script.find("renewed") != std::string::npos) {
                EXPECT_EQ(args[2], "endpoint|nonce");
                if (args[1] == "2") {
                    entered.set_value();
                    EXPECT_EQ(gate.wait_for(2s), std::future_status::ready);
                }
                std::lock_guard<std::mutex> lock(actions_mutex);
                actions.push_back("renewed " + args[1]);
                return InstanceLease::Values{"renewed"};
            }
            EXPECT_EQ(args[1], "endpoint|nonce");
            actions.push_back("removed");
            return InstanceLease::Values{};
        },
        "test",
        "endpoint|",
        "nonce");
    ASSERT_TRUE(lease.Register());
    lease.Renew(2);
    EXPECT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
    lease.Renew(3);
    lease.Renew(4);
    release.set_value();
    lease.Unregister();
    EXPECT_EQ(actions, (InstanceLease::Values{"registered", "renewed 2", "renewed 4", "removed"}));
}

// 观察 Stop 已关闭入口；在屏障处放行前不要仅凭异步 Stop 已启动推测时序。
bool WaitForClosedIngress(LogicDispatcher& dispatcher, std::future<void>& stopped) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (dispatcher.Submit({nullptr, 0, {}}) == LogicSubmitResult::Closed)
            return stopped.wait_for(0ms) != std::future_status::ready;
        stopped.wait_for(1ms);
    }
    return false;
}

// T08-LOGIC-15：同片另一连接可先处理，写请求跨两次回调保持归属直到最终收尾。
TEST(LogicDispatcherTests, ReadWaitsForFinalContinuationOnItsOwningShard) {
    const auto writer = SessionKey(), neighbor = SessionKey();
    std::promise<LogicDispatcher::Resume> auth, sql;
    std::promise<void> heartbeat, finalized;
    std::thread::id owner;
    bool committed = false;
    LogicDispatcher* executor = nullptr;
    // 用显式回调控制认证和 SQL 两次返回，覆盖一次请求连续交接顺序位置。
    LogicDispatcher dispatcher([&](const LogicMessage& message) {
        if (message.id == 0)
            return true;
        if (message.id == 1) {
            owner = std::this_thread::get_id();
            auth.set_value(executor->AwaitCompletion(writer));
        } else if (message.id == 2) {
            EXPECT_EQ(std::this_thread::get_id(), owner);
            EXPECT_TRUE(committed);
            finalized.set_value();
        } else {
            EXPECT_EQ(std::this_thread::get_id(), owner);
            heartbeat.set_value();
        }
        return true;
    });
    executor = &dispatcher;
    dispatcher.Submit({writer, 1, {}, 0, 1});
    dispatcher.Submit({writer, 2, {}, 0, 1});
    dispatcher.Submit({neighbor, 3, {}, 0, 1});
    auto first = auth.get_future();
    ASSERT_EQ(first.wait_for(2s), std::future_status::ready);
    auto resume_auth = first.get();
    EXPECT_EQ(heartbeat.get_future().wait_for(2s), std::future_status::ready);
    // 认证完成仍在原片，并把同一请求继续交给 SQL。
    resume_auth([&] {
        EXPECT_EQ(std::this_thread::get_id(), owner);
        sql.set_value(executor->AwaitCompletion(writer));
    });
    auto second = sql.get_future();
    ASSERT_EQ(second.wait_for(2s), std::future_status::ready);
    // 模拟在停服排空期间交回数据库结果。
    auto stop = std::async(std::launch::async, [&] {
        dispatcher.Stop();
    });
    EXPECT_TRUE(WaitForClosedIngress(dispatcher, stop));
    auto resume_sql = second.get();
    // 业务已确认提交，后续读取才能看到。
    resume_sql([&] {
        EXPECT_EQ(std::this_thread::get_id(), owner);
        committed = true;
    });
    EXPECT_EQ(finalized.get_future().wait_for(2s), std::future_status::ready);
    EXPECT_EQ(stop.wait_for(2s), std::future_status::ready);
    stop.get();
}

// 测试通过真实业务分片把已提交的通知交给进程级 FIFO。
class NotificationDispatcher final : public LogicDispatcher {
public:
    using LogicDispatcher::LogicDispatcher;
    using LogicDispatcher::QueueNotification;
};

// T08-LOGIC-16：来自两片的通知共用顺序，停服排空依赖回调后才能结束。
TEST(LogicDispatcherTests, NotificationsFromBothShardsDrainOneFifo) {
    std::promise<void> entered, release, queued;
    auto gate = release.get_future().share();
    std::vector<int> deliveries;
    NotificationDispatcher* executor = nullptr;
    // 首次通知阻塞在出站池，第二个分片仍能受理后续通知。
    NotificationDispatcher dispatcher([&](const LogicMessage& message) {
        if (message.id == 0)
            return true;
        const int id = message.id;
        // 路由完成后返回出站任务。
        executor->QueueNotification([&, id]() -> LogicDispatcher::Completion {
            EXPECT_TRUE(DependencyExecutor::RunningOn(ExternalTask::Redis));
            // 单消费者应该按通知入队顺序完成两次投递。
            return [&, id] {
                if (id == 1) {
                    entered.set_value();
                    EXPECT_EQ(gate.wait_for(3s), std::future_status::ready);
                }
                deliveries.push_back(id);
            };
        });
        if (id == 2)
            queued.set_value();
        return true;
    });
    executor = &dispatcher;
    dispatcher.Submit({SessionKey(), 1, {}, 0, 1});
    ASSERT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
    dispatcher.Submit({SessionKey(), 2, {}, 0, 0});
    EXPECT_EQ(queued.get_future().wait_for(2s), std::future_status::ready);
    // 独立通知在业务入口关闭后仍可返回。
    auto stopped = std::async(std::launch::async, [&] {
        dispatcher.Stop();
    });
    EXPECT_TRUE(WaitForClosedIngress(dispatcher, stopped));
    release.set_value();
    EXPECT_EQ(stopped.wait_for(2s), std::future_status::ready);
    stopped.get();
    EXPECT_EQ(deliveries, (std::vector<int>{1, 2}));
}

} // namespace
