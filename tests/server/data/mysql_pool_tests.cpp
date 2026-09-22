#include "../../../common/mysql/ConnectionPool.h"
#include <gtest/gtest.h>
#include <future>

namespace {
using namespace std::chrono_literals;
struct Connection {
    bool valid = true;
    bool autocommit = true;
    bool fail_rollback = false;
    int rollbacks = 0;
    std::function<void()> validation;
    bool isValid() { if (validation) validation(); return valid; }
    bool isClosed() { return false; }
    bool getAutoCommit() { return autocommit; }
    void setAutoCommit(bool value) { autocommit = value; }
    void rollback() {
        if (fail_rollback) throw std::runtime_error("rollback failed");
        ++rollbacks;
    }
};
using Pool = chat_mysql::ConnectionPool<Connection>;

TEST(MysqlConnectionPoolTests, ExhaustionHasFiniteWaitAndReturnedConnectionCanBeBorrowed) {
    Pool pool(1, [] { return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    const auto started = Pool::Clock::now();
    EXPECT_EQ(pool.Borrow(started + 30ms), nullptr);
    EXPECT_GE(Pool::Clock::now() - started, 20ms);
    EXPECT_LT(Pool::Clock::now() - started, 1s);
    pool.Return(std::move(held));
    auto lease = pool.Acquire();
    EXPECT_TRUE(lease->valid);
}

TEST(MysqlConnectionPoolTests, CloseWakesWaitersAndRejectsReturnedConnections) {
    Pool pool(1, [] { return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    std::promise<void> entered;
    auto waiting = std::async(std::launch::async, [&] {
        entered.set_value();
        return pool.Borrow(Pool::Clock::now() + 2s);
    });
    entered.get_future().wait();
    pool.Close();
    pool.Close();
    ASSERT_EQ(waiting.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(waiting.get(), nullptr);
    pool.Return(std::move(held));
    EXPECT_EQ(pool.Borrow(), nullptr);
}

TEST(MysqlConnectionPoolTests, InvalidIdleConnectionIsReplacedBeforeBorrow) {
    int created = 0;
    Pool pool(1, [&] { ++created; return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    held->valid = false;
    pool.Return(std::move(held));
    auto lease = pool.Acquire();
    EXPECT_TRUE(lease->valid);
    EXPECT_EQ(created, 2);
}

TEST(MysqlConnectionPoolTests, FailedReplacementReleasesCapacityForRecovery) {
    bool unavailable = false;
    Pool pool(1, [&] {
        if (unavailable) throw std::runtime_error("unavailable");
        return std::make_unique<Connection>();
    });
    auto held = pool.Borrow();
    held->valid = false;
    pool.Return(std::move(held));
    unavailable = true;
    EXPECT_EQ(pool.Borrow(), nullptr);
    unavailable = false;
    auto lease = pool.Acquire();
    EXPECT_TRUE(lease->valid);
}

TEST(MysqlConnectionPoolTests, ReturnRollsBackUnfinishedWorkAndDiscardsResetFailures) {
    int created = 0;
    Pool pool(1, [&] { ++created; return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    held->autocommit = false;
    pool.Return(std::move(held));
    held = pool.Borrow();
    EXPECT_TRUE(held->autocommit);
    EXPECT_EQ(held->rollbacks, 1);
    held->autocommit = false;
    held->fail_rollback = true;
    EXPECT_NO_THROW(pool.Return(std::move(held)));
    auto lease = pool.Acquire();
    EXPECT_TRUE(lease->autocommit);
    EXPECT_EQ(created, 2);
}

TEST(MysqlConnectionPoolTests, SlowHealthCheckDoesNotHoldPoolMutexOrEscapeClose) {
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto released = release.get_future().share();
    Pool pool(1, [&] {
        auto connection = std::make_unique<Connection>();
        connection->validation = [&] { entered.set_value(); released.wait(); };
        return connection;
    });
    auto borrow = std::async(std::launch::async, [&] { return pool.Borrow(); });
    const auto entered_status = started.wait_for(500ms);
    auto closing = std::async(std::launch::async, [&] { pool.Close(); });
    const auto close_status = closing.wait_for(500ms);
    release.set_value();
    closing.get();
    EXPECT_EQ(entered_status, std::future_status::ready);
    EXPECT_EQ(close_status, std::future_status::ready);
    EXPECT_EQ(borrow.get(), nullptr);
}
} // namespace
