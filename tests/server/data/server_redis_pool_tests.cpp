#include <gtest/gtest.h>

#include "RedisMgr.h"
#include "../../../common/redis/Reply.h"

#include <chrono>
#include <future>

namespace {

// T04-RDS-01
/** 验证真实 Redis 空池关闭会唤醒借用者，无需外部服务连接。 */
TEST(ServerRedisPoolTests, CloseWakesAWaitingBorrowerWithoutAServiceConnection) {
    RedisConnectionPool pool("127.0.0.1", "1", "test-placeholder", 0);
    std::promise<void> borrower_started;
    auto borrower = std::async(std::launch::async, /** 通知等待已开始并以有限期限借用连接。 */ [&]() {
        borrower_started.set_value();
        return pool.GetConnection(std::chrono::milliseconds(500));
    });
    borrower_started.get_future().wait();

    pool.Close();

    ASSERT_EQ(borrower.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(borrower.get(), nullptr);
}

// T04-RDS-02
/** 验证重复关闭幂等且后续借用立即失败。 */
TEST(ServerRedisPoolTests, CloseIsIdempotentAndFutureBorrowsFailImmediately) {
    RedisConnectionPool pool("127.0.0.1", "1", "test-placeholder", 0);

    EXPECT_NO_THROW(pool.Close());
    EXPECT_NO_THROW(pool.Close());

    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(pool.GetConnection(std::chrono::seconds(1)), nullptr);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(100));
}

// T04-RDS-03
/** 验证耗尽池的借用在有限等待到期后返回空。 */
TEST(ServerRedisPoolTests, ExhaustedBorrowReturnsWhenItsFiniteWaitExpires) {
    RedisConnectionPool pool("127.0.0.1", "1", "test-placeholder", 0);

    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(pool.GetConnection(std::chrono::milliseconds(50)), nullptr);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_GE(elapsed, std::chrono::milliseconds(25));
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

/** 验证缺失、错误与含零字节 Redis 回复具有明确结果。 */
TEST(ServerRedisReplyTests, MissingErrorAndBinaryRepliesHaveUnambiguousResults) {
    std::string value = "old";
    EXPECT_FALSE(chat_redis::ReadString(nullptr, value));
    EXPECT_TRUE(value.empty());
    redisReply reply = {};
    for (int type : {REDIS_REPLY_NIL, REDIS_REPLY_ERROR, REDIS_REPLY_INTEGER}) {
        reply.type = type;
        EXPECT_FALSE(chat_redis::ReadString(&reply, value));
    }
    char bytes[] = {'a', '\0', 'b'};
    reply.type = REDIS_REPLY_STRING;
    reply.str = bytes;
    reply.len = sizeof(bytes);
    EXPECT_TRUE(chat_redis::ReadString(&reply, value));
    EXPECT_EQ(value, std::string(bytes, sizeof(bytes)));
    reply.type = REDIS_REPLY_INTEGER;
    reply.integer = 1;
    EXPECT_TRUE(chat_redis::IsPositiveInteger(&reply));
    reply.integer = 0;
    EXPECT_FALSE(chat_redis::IsPositiveInteger(&reply));
    reply.type = REDIS_REPLY_ERROR;
    reply.integer = 1;
    EXPECT_FALSE(chat_redis::IsPositiveInteger(&reply));
}

} // namespace
