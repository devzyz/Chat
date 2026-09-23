#include <gtest/gtest.h>

#include "AsioIOServicePool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {

/** 共享任务完成计数与有界等待的同步状态。 */
struct CompletionState {
    std::atomic<int> completed{0};
    std::mutex mutex;
    std::condition_variable condition;
};

/** 验证已提交任务在作用域销毁前完成且仅执行一次。 */
TEST(AsioPoolContractTests, PostedTaskCompletesBeforeScopedDestruction) {
    auto state = std::make_shared<CompletionState>();
    {
        AsioIOServicePool pool(1);
        boost::asio::post(pool.GetIOService(), /** 递增完成计数并唤醒测试线程。 */ [state]() {
            state->completed.fetch_add(1);
            state->condition.notify_all();
        });

        std::unique_lock<std::mutex> lock(state->mutex);
        ASSERT_TRUE(state->condition.wait_for(
            lock,
            std::chrono::seconds(2),
            /** 检测测试任务是否恰好完成一次。 */ [state]() { return state->completed.load() == 1; }));
    }
    EXPECT_EQ(state->completed.load(), 1);
}

/** 验证重复停止与随后析构均在期限内完成且不抛异常。 */
TEST(AsioPoolContractTests, RepeatedStopAndDestructionCompleteWithinTheDeadline) {
    const auto started = std::chrono::steady_clock::now();
    {
        AsioIOServicePool pool(1);
        EXPECT_NO_THROW(pool.Stop());
        EXPECT_NO_THROW(pool.Stop());
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
}

} // namespace
