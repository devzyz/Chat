#include <gtest/gtest.h>

#include "RedisMgr.h"

#include <chrono>
#include <future>

namespace {

// T04-RDS-01
TEST(ServerRedisPoolTests, CloseWakesAWaitingBorrowerWithoutAServiceConnection) {
    RedisConnectionPool pool("127.0.0.1", "1", "test-placeholder", 0);
    std::promise<void> borrower_started;
    auto borrower = std::async(std::launch::async, [&]() {
        borrower_started.set_value();
        return pool.getConnection(std::chrono::milliseconds(500));
    });
    borrower_started.get_future().wait();

    pool.close();

    ASSERT_EQ(borrower.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(borrower.get(), nullptr);
}

// T04-RDS-02
TEST(ServerRedisPoolTests, CloseIsIdempotentAndFutureBorrowsFailImmediately) {
    RedisConnectionPool pool("127.0.0.1", "1", "test-placeholder", 0);

    EXPECT_NO_THROW(pool.close());
    EXPECT_NO_THROW(pool.close());

    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(pool.getConnection(std::chrono::seconds(1)), nullptr);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(100));
}

// T04-RDS-03
TEST(ServerRedisPoolTests, ExhaustedBorrowReturnsWhenItsFiniteWaitExpires) {
    RedisConnectionPool pool("127.0.0.1", "1", "test-placeholder", 0);

    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(pool.getConnection(std::chrono::milliseconds(50)), nullptr);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_GE(elapsed, std::chrono::milliseconds(25));
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

} // namespace
