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
        return pool.getConnection();
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

    auto borrow = std::async(std::launch::async, [&]() { return pool.getConnection(); });
    ASSERT_EQ(borrow.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(borrow.get(), nullptr);
}

} // namespace
