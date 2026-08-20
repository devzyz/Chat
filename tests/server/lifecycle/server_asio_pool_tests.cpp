#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#define Singleton GateSingleton
#define AsioIOServicePool GateAsioIOServicePool
#include "../../../GateServer/GateServer/AsioIOServicePool.h"
#include "../../../GateServer/GateServer/AsioIOServicePool.cpp"
#undef AsioIOServicePool
#undef Singleton

#define Singleton StatusSingleton
#define AsioIOServicePool StatusAsioIOServicePool
#include "../../../StatusServer/StatusServer/AsioIOServicePool.h"
#include "../../../StatusServer/StatusServer/AsioIOServicePool.cpp"
#undef AsioIOServicePool
#undef Singleton

namespace {

template <typename Pool>
void ExpectTaskExecution(Pool& pool) {
    std::atomic<bool> completed{false};
    std::mutex mutex;
    std::condition_variable condition;

    boost::asio::post(pool.GetIOService(), [&]() {
        completed = true;
        condition.notify_one();
    });

    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(
        lock,
        std::chrono::seconds(5),
        [&]() { return completed.load(); }));
}

// T03-GATE-01
TEST(ServerAsioPoolLifecycleTests, GatePoolExecutesQueuedWorkBeforeDestructorShutdown) {
    auto pool = GateAsioIOServicePool::GetInstance();
    ExpectTaskExecution(*pool);
}

// T03-STATUS-01
TEST(ServerAsioPoolLifecycleTests, StatusPoolExecutesQueuedWorkBeforeDestructorShutdown) {
    auto& pool = StatusAsioIOServicePool::GetInstance();
    ExpectTaskExecution(*pool);
}

} // namespace
