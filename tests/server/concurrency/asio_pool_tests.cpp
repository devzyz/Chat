#include <gtest/gtest.h>

#include "AsioIOServicePool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

TEST(AsioIOServicePoolTests, DispatchesQueuedWorkAndStopsIdempotently) {
    auto pool = AsioIOServicePool::GetInstance();
    constexpr int task_count = 16;
    std::atomic<int> completed{0};
    std::mutex mutex;
    std::condition_variable all_tasks_completed;

    for (int index = 0; index < task_count; ++index) {
        auto& io_context = pool->GetIOService();
        boost::asio::post(io_context, [&]() {
            if (completed.fetch_add(1) + 1 == task_count) {
                std::lock_guard<std::mutex> lock(mutex);
                all_tasks_completed.notify_one();
            }
        });
    }

    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(all_tasks_completed.wait_for(
        lock,
        std::chrono::seconds(5),
        [&]() { return completed.load() == task_count; }));
    EXPECT_EQ(completed.load(), task_count);

    EXPECT_NO_THROW(pool->stop());
    EXPECT_NO_THROW(pool->stop());
}

} // namespace
