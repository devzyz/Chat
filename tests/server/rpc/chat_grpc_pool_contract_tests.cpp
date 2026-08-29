#include <gtest/gtest.h>

#include "../../../ChatServer/ChatServer/ChatGrpcClient.h"
#include "../../../ChatServer/ChatServer/StatusGrpcClient.h"

#include <chrono>
#include <future>
#include <memory>

using namespace std::chrono_literals;

namespace {
template <typename Factory>
void VerifyBorrowReturn(Factory factory) {
    auto pool = factory();
    {
        auto first = pool->Acquire();
        ASSERT_TRUE(first);
    }
    EXPECT_TRUE(pool->Acquire());
}

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

template <typename Factory>
void VerifyCloseLifecycle(Factory factory) {
    auto pool = factory();
    auto held = pool->Acquire();
    ASSERT_TRUE(held);
    std::promise<void> entering;
    auto entered = entering.get_future();
    auto waiter = std::async(std::launch::async, [&] {
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

auto ChatStatusPoolFactory() {
    return [] { return std::make_unique<StatusConnectionPool>("127.0.0.1", "1", 1, 50ms); };
}

auto ChatPeerPoolFactory() {
    return [] { return std::make_unique<ChatConnectionPool>("127.0.0.1", "1", 1, 50ms); };
}
}

TEST(ChatStatusPoolContractTests, BorrowReturn) { VerifyBorrowReturn(ChatStatusPoolFactory()); }
TEST(ChatStatusPoolContractTests, ExhaustionIsBounded) { VerifyExhaustion(ChatStatusPoolFactory()); }
TEST(ChatStatusPoolContractTests, CloseIsIdempotentAndWakesBorrowers) { VerifyCloseLifecycle(ChatStatusPoolFactory()); }

TEST(ChatPeerPoolContractTests, BorrowReturn) { VerifyBorrowReturn(ChatPeerPoolFactory()); }
TEST(ChatPeerPoolContractTests, ExhaustionIsBounded) { VerifyExhaustion(ChatPeerPoolFactory()); }
TEST(ChatPeerPoolContractTests, CloseIsIdempotentAndWakesBorrowers) { VerifyCloseLifecycle(ChatPeerPoolFactory()); }
