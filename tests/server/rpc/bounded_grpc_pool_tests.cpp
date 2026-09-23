#include <gtest/gtest.h>

#include "../../../common/grpc/GrpcClientRuntime.h"

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

using namespace std::chrono_literals;

/** 验证池耗尽在配置借用期限内返回明确失败。 */
TEST(BoundedGrpcPoolTests, ExhaustedPoolReturnsWithinConfiguredAcquireTimeout) {
    rpc::BoundedPool<int> pool(1, 50ms, /** 创建固定值的独占池资源。 */ [] {
        return std::make_unique<int>(7);
    });

    auto first = pool.Acquire();
    ASSERT_TRUE(first);

    const auto started = std::chrono::steady_clock::now();
    auto exhausted = pool.Acquire();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_FALSE(exhausted);
    EXPECT_EQ(exhausted.failure, rpc::Failure::PoolExhausted);
    EXPECT_GE(elapsed, 40ms);
    EXPECT_LT(elapsed, 500ms);
}

/** 验证租约析构归还后可再次借用。 */
TEST(BoundedGrpcPoolTests, ReturnedLeaseCanBeBorrowedAgain) {
    rpc::BoundedPool<int> pool(1, 50ms, /** 创建固定值的独占池资源。 */ [] { return std::make_unique<int>(7); });
    {
        auto borrowed = pool.Acquire();
        ASSERT_TRUE(borrowed);
        EXPECT_EQ(*borrowed.lease, 7);
    }
    EXPECT_TRUE(pool.Acquire());
}

/** 验证关闭立即唤醒借用者，无需等待原借用期限。 */
TEST(BoundedGrpcPoolTests, CloseWakesWaitingBorrowerWithoutWaitingForAcquireTimeout) {
    rpc::BoundedPool<int> pool(1, 5s, /** 创建固定值的独占池资源。 */ [] { return std::make_unique<int>(7); });
    auto held = pool.Acquire();
    ASSERT_TRUE(held);

    std::promise<void> entering_acquire;
    auto entered = entering_acquire.get_future();
    auto waiter = std::async(std::launch::async, /** 通知借用即将开始并返回等待后的失败分类。 */ [&] {
        entering_acquire.set_value();
        return pool.Acquire().failure;
    });
    ASSERT_EQ(entered.wait_for(500ms), std::future_status::ready);

    pool.Close();

    ASSERT_EQ(waiter.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(waiter.get(), rpc::Failure::Closed);
}

/** 验证重复关闭及关闭后归还不会重新开放池。 */
TEST(BoundedGrpcPoolTests, RepeatedCloseAndReturnAfterCloseRemainClosed) {
    rpc::BoundedPool<int> pool(1, 50ms, /** 创建固定值的独占池资源。 */ [] { return std::make_unique<int>(7); });
    auto held = pool.Acquire();
    ASSERT_TRUE(held);

    pool.Close();
    pool.Close();
    held.lease.Reset();

    const auto after_close = pool.Acquire();
    EXPECT_FALSE(after_close);
    EXPECT_EQ(after_close.failure, rpc::Failure::Closed);
}

/** 验证 gRPC 状态映射为稳定的失败类别。 */
TEST(GrpcClientRuntimeTests, ClassifiesStableGrpcFailureCategories) {
    EXPECT_EQ(rpc::ClassifyStatus(grpc::Status::OK), rpc::Failure::None);
    EXPECT_EQ(rpc::ClassifyStatus({ grpc::StatusCode::DEADLINE_EXCEEDED, "test" }), rpc::Failure::DeadlineExceeded);
    EXPECT_EQ(rpc::ClassifyStatus({ grpc::StatusCode::UNAVAILABLE, "test" }), rpc::Failure::Unavailable);
    EXPECT_EQ(rpc::ClassifyStatus({ grpc::StatusCode::CANCELLED, "test" }), rpc::Failure::Cancelled);
    EXPECT_EQ(rpc::ClassifyStatus({ grpc::StatusCode::INTERNAL, "test" }), rpc::Failure::Other);
}

/** 验证默认期限、合法边界及非法配置拒绝。 */
TEST(GrpcClientRuntimeTests, DurationUsesDefaultOrValidatesConfiguredRange) {
    EXPECT_EQ(rpc::ParseDurationMs("", "deadline", 3s), 3s);
    EXPECT_EQ(rpc::ParseDurationMs("100", "deadline", 3s), 100ms);
    EXPECT_EQ(rpc::ParseDurationMs("60000", "deadline", 3s), 60s);
    EXPECT_THROW(rpc::ParseDurationMs("99", "deadline", 3s), std::invalid_argument);
    EXPECT_THROW(rpc::ParseDurationMs("60001", "deadline", 3s), std::invalid_argument);
    EXPECT_THROW(rpc::ParseDurationMs("not-a-number", "deadline", 3s), std::invalid_argument);
}
