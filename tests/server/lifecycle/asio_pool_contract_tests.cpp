#include <gtest/gtest.h>

#include "AsioIOServicePool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {

struct CompletionState {
    std::atomic<int> completed{0};
    std::mutex mutex;
    std::condition_variable condition;
};

TEST(AsioPoolContractTests, PostedTaskCompletesBeforeScopedDestruction) {
    auto state = std::make_shared<CompletionState>();
    {
        AsioIOServicePool pool(1);
        boost::asio::post(pool.GetIOService(), [state]() {
            state->completed.fetch_add(1);
            state->condition.notify_all();
        });

        std::unique_lock<std::mutex> lock(state->mutex);
        ASSERT_TRUE(state->condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [state]() { return state->completed.load() == 1; }));
    }
    EXPECT_EQ(state->completed.load(), 1);
}

TEST(AsioPoolContractTests, RepeatedStopAndDestructionCompleteWithinTheDeadline) {
    const auto started = std::chrono::steady_clock::now();
    {
        AsioIOServicePool pool(1);
        EXPECT_NO_THROW(pool.stop());
        EXPECT_NO_THROW(pool.stop());
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
}

} // namespace
