#include "../../../common/mysql/ConnectionPool.h"
#include <gtest/gtest.h>
#include <future>

namespace {
using namespace std::chrono_literals;
/** 模拟 Connector/C++ 连接健康与事务状态，允许注入校验阻塞和回滚失败。 */
struct Connection {
    bool valid = true;
    bool autocommit = true;
    bool fail_rollback = false;
    int rollbacks = 0;
    std::function<void()> validation;
    /** 执行可选健康探针后返回有效状态；接口拼写由连接池模板约束。 @see sql::Connection::isValid。 */
    bool isValid() { if (validation) validation(); return valid; }
    /** 模拟连接未关闭。 @see sql::Connection::isClosed。 */
    bool isClosed() { return false; }
    /** 返回模拟自动提交状态。 @see sql::Connection::getAutoCommit。 */
    bool getAutoCommit() { return autocommit; }
    /** 更新模拟自动提交状态。 @see sql::Connection::setAutoCommit。 */
    void setAutoCommit(bool value) { autocommit = value; }
    /** 按故障开关抛出回滚错误，否则累计回滚次数。 @see sql::Connection::rollback。 */
    void rollback() {
        if (fail_rollback) throw std::runtime_error("rollback failed");
        ++rollbacks;
    }
};
using Pool = chat_mysql::ConnectionPool<Connection>;

/** 验证池耗尽等待有期限，归还后可再次借用。 */
TEST(MysqlConnectionPoolTests, ExhaustionHasFiniteWaitAndReturnedConnectionCanBeBorrowed) {
    Pool pool(1, /** 创建健康连接替身。 */ [] { return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    const auto started = Pool::Clock::now();
    EXPECT_EQ(pool.Borrow(started + 30ms), nullptr);
    EXPECT_GE(Pool::Clock::now() - started, 20ms);
    EXPECT_LT(Pool::Clock::now() - started, 1s);
    pool.Return(std::move(held));
    auto lease = pool.Acquire();
    EXPECT_TRUE(lease->valid);
}

/** 验证关闭唤醒等待者，重复关闭及归还不使池复活。 */
TEST(MysqlConnectionPoolTests, CloseWakesWaitersAndRejectsReturnedConnections) {
    Pool pool(1, /** 创建健康连接替身。 */ [] { return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    std::promise<void> entered;
    auto waiting = std::async(std::launch::async, /** 通知借用已开始并在有限期限内等待连接。 */ [&] {
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

/** 验证空闲连接失效后在借出前被替换。 */
TEST(MysqlConnectionPoolTests, InvalidIdleConnectionIsReplacedBeforeBorrow) {
    int created = 0;
    Pool pool(1, /** 创建连接并累计工厂调用次数。 */ [&] { ++created; return std::make_unique<Connection>(); });
    auto held = pool.Borrow();
    held->valid = false;
    pool.Return(std::move(held));
    auto lease = pool.Acquire();
    EXPECT_TRUE(lease->valid);
    EXPECT_EQ(created, 2);
}

/** 验证替换连接失败释放容量，依赖恢复后仍能借用。 */
TEST(MysqlConnectionPoolTests, FailedReplacementReleasesCapacityForRecovery) {
    bool unavailable = false;
    Pool pool(1, /** 根据故障开关拒绝创建或返回健康连接。 */ [&] {
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

/** 验证归还回滚未完成事务并丢弃清理失败连接。 */
TEST(MysqlConnectionPoolTests, ReturnRollsBackUnfinishedWorkAndDiscardsResetFailures) {
    int created = 0;
    Pool pool(1, /** 创建连接并累计工厂调用次数。 */ [&] { ++created; return std::make_unique<Connection>(); });
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

/** 验证慢健康检查不持有池互斥锁且不能越过关闭状态借出。 */
TEST(MysqlConnectionPoolTests, SlowHealthCheckDoesNotHoldPoolMutexOrEscapeClose) {
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto released = release.get_future().share();
    Pool pool(1, /** 构造带阻塞健康探针的连接。 */ [&] {
        auto connection = std::make_unique<Connection>();
        connection->validation = /** 通知探针已进入并等待测试释放屏障。 */ [&] { entered.set_value(); released.wait(); };
        return connection;
    });
    auto borrow = std::async(std::launch::async, /** 在独立任务中触发连接借用与健康校验。 */ [&] { return pool.Borrow(); });
    const auto entered_status = started.wait_for(500ms);
    auto closing = std::async(std::launch::async, /** 并发关闭连接池以验证不被健康检查阻塞。 */ [&] { pool.Close(); });
    const auto close_status = closing.wait_for(500ms);
    release.set_value();
    closing.get();
    EXPECT_EQ(entered_status, std::future_status::ready);
    EXPECT_EQ(close_status, std::future_status::ready);
    EXPECT_EQ(borrow.get(), nullptr);
}
} // namespace
