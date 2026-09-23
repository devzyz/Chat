#include <gtest/gtest.h>

#include "../../../ChatServer/ChatServer/ChatGrpcClient.h"
#include "../../../ChatServer/ChatServer/StatusGrpcClient.h"

#include <chrono>
#include <future>
#include <memory>

using namespace std::chrono_literals;

namespace {
/** 验证具体生产池的租约归还后可再次借用。 */
template <typename Factory>
void VerifyBorrowReturn(Factory factory) {
    auto pool = factory();
    {
        auto first = pool->Acquire();
        ASSERT_TRUE(first);
    }
    EXPECT_TRUE(pool->Acquire());
}

/** 验证具体生产池耗尽时有限返回 PoolExhausted。 */
template <typename Factory>
void VerifyExhaustion(Factory factory) {
    auto pool = factory();
    auto held = pool->Acquire();
    ASSERT_TRUE(held);
    const auto started = std::chrono::steady_clock::now();
    const auto exhausted = pool->Acquire();
    EXPECT_EQ(exhausted.failure, rpc::Failure::PoolExhausted);
    EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
}

/** 验证生产池关闭幂等、唤醒等待者并拒绝关闭后借用。 */
template <typename Factory>
void VerifyCloseLifecycle(Factory factory) {
    auto pool = factory();
    auto held = pool->Acquire();
    ASSERT_TRUE(held);
    std::promise<void> entering;
    auto entered = entering.get_future();
    auto waiter = std::async(std::launch::async, /** 通知借用任务已进入并等待池关闭结果。 */ [&] {
        entering.set_value();
        return pool->Acquire().failure;
    });
    ASSERT_EQ(entered.wait_for(500ms), std::future_status::ready);
    pool->Close();
    pool->Close();
    ASSERT_EQ(waiter.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(waiter.get(), rpc::Failure::Closed);
    held.lease.Reset();
    EXPECT_EQ(pool->Acquire().failure, rpc::Failure::Closed);
}

/** 返回创建Chat 的 Status单容量短期限池的工厂。 */
auto ChatStatusPoolFactory() {
    return /** 创建Chat 的 Status生产 RPC 池，无需实际调用远端。 */ [] { return std::make_unique<StatusConnectionPool>("127.0.0.1", "1", 1, 50ms); };
}

/** 返回创建Chat 对端单容量短期限池的工厂。 */
auto ChatPeerPoolFactory() {
    return /** 创建Chat 对端生产 RPC 池，无需实际调用远端。 */ [] { return std::make_unique<ChatConnectionPool>("127.0.0.1", "1", 1, 50ms); };
}
}

/** 验证Chat 的 Status池借用归还合同。 */
TEST(ChatStatusPoolContractTests, BorrowReturn) { VerifyBorrowReturn(ChatStatusPoolFactory()); }
/** 验证Chat 的 Status池耗尽有界合同。 */
TEST(ChatStatusPoolContractTests, ExhaustionIsBounded) { VerifyExhaustion(ChatStatusPoolFactory()); }
/** 验证Chat 的 Status池关闭唤醒与幂等合同。 */
TEST(ChatStatusPoolContractTests, CloseIsIdempotentAndWakesBorrowers) { VerifyCloseLifecycle(ChatStatusPoolFactory()); }

/** 验证Chat 对端池借用归还合同。 */
TEST(ChatPeerPoolContractTests, BorrowReturn) { VerifyBorrowReturn(ChatPeerPoolFactory()); }
/** 验证Chat 对端池耗尽有界合同。 */
TEST(ChatPeerPoolContractTests, ExhaustionIsBounded) { VerifyExhaustion(ChatPeerPoolFactory()); }
/** 验证Chat 对端池关闭唤醒与幂等合同。 */
TEST(ChatPeerPoolContractTests, CloseIsIdempotentAndWakesBorrowers) { VerifyCloseLifecycle(ChatPeerPoolFactory()); }
