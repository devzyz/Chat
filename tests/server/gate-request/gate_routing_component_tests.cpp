#include <gtest/gtest.h>

#include "../../../GateServer/GateServer/GateRouting.h"

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

constexpr int kSuccess = 0;
constexpr int kRpcFailed = 1002;

/** 模拟可成功、返回失败或抛异常的 Token 存储，数据访问受互斥锁保护。 */
class InMemoryAssignmentStore final : public gate_routing::AssignmentStore {
public:
    enum class PutBehavior { Succeed, ReturnFalse, Throw };

    /** 保存本用例指定的 Token 写入故障模式。 */
    explicit InMemoryAssignmentStore(PutBehavior put_behavior = PutBehavior::Succeed) : put_behavior_(put_behavior) {}

    /** 提供固定零连接数，排除负载排序干扰。 */
    std::optional<std::string> ReadCount(const std::string&) override {
        return "0";
    }

    /** 按故障模式失败或在锁内保存 Token。 */
    bool PutToken(int uid, const std::string& token, long long) override {
        if (put_behavior_ == PutBehavior::Throw) {
            throw std::runtime_error("SYNTHETIC_INTERNAL_EXCEPTION_3A02");
        }
        if (put_behavior_ == PutBehavior::ReturnFalse) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_[uid] = token;
        return true;
    }

private:
    PutBehavior put_behavior_;
    std::mutex mutex_;
    std::unordered_map<int, std::string> tokens_;
};

/** 提供确定性的测试 Token 生成器。 */
class SyntheticTokenSource final : public gate_routing::TokenSource {
public:
    /** 返回固定测试 Token。 */
    std::string Next() override {
        return "SYNTHETIC_STATUS_TOKEN_3A02";
    }
};

// T08-STATUS-09
/** 验证 Token 写入返回失败时清空选服结果。 */
TEST(GateRoutingComponentTests, StoreFalseFailsClosedAndClearsAssignment) {
    auto routing = gate_routing::CreateGateRouting(
        {{"chat-a", "127.0.0.1", "9001"}},
        std::make_shared<InMemoryAssignmentStore>(InMemoryAssignmentStore::PutBehavior::ReturnFalse),
        std::make_shared<SyntheticTokenSource>());

    const auto result = routing->Assign(51);
    EXPECT_EQ(result.error, kRpcFailed);
    EXPECT_TRUE(result.host.empty());
    EXPECT_TRUE(result.port.empty());
    EXPECT_TRUE(result.token.empty());
}

// T08-STATUS-08
/** 验证写入成功时返回完整端点与 Token。 */
TEST(GateRoutingComponentTests, SuccessfulStoreProducesCompleteAssignment) {
    auto routing = gate_routing::CreateGateRouting(
        {{"chat-a", "127.0.0.1", "9001"}},
        std::make_shared<InMemoryAssignmentStore>(),
        std::make_shared<SyntheticTokenSource>());
    const auto result = routing->Assign(52);
    EXPECT_EQ(result.error, kSuccess);
    EXPECT_FALSE(result.host.empty());
    EXPECT_FALSE(result.port.empty());
    EXPECT_FALSE(result.token.empty());
}

// T08-STATUS-10
/** 验证存储异常被转换为稳定的失败响应。 */
TEST(GateRoutingComponentTests, StoreExceptionUsesStableFailClosedEnvelope) {
    auto routing = gate_routing::CreateGateRouting(
        {{"chat-a", "127.0.0.1", "9001"}},
        std::make_shared<InMemoryAssignmentStore>(InMemoryAssignmentStore::PutBehavior::Throw),
        std::make_shared<SyntheticTokenSource>());
    const auto result = routing->Assign(53);
    EXPECT_EQ(result.error, kRpcFailed);
    EXPECT_TRUE(result.host.empty());
    EXPECT_TRUE(result.port.empty());
    EXPECT_TRUE(result.token.empty());
}

} // namespace
