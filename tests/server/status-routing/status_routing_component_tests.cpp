#include <gtest/gtest.h>

#include "StatusRouting.h"
#include "StatusRoutingInternal.h"

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

constexpr int kSuccess = 0;
constexpr int kRpcFailed = 1002;
constexpr int kUidInvalid = 1010;
constexpr int kTokenInvalid = 1011;

/** 模拟可成功、返回失败或抛异常的 Token 存储，数据访问受互斥锁保护。 */
class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	enum class PutBehavior { Succeed, ReturnFalse, Throw };

	/** 保存本用例指定的 Token 写入故障模式。 */
	explicit InMemoryStatusStore(PutBehavior put_behavior = PutBehavior::Succeed)
		: put_behavior_(put_behavior) {
	}

	/** 提供固定零连接数，排除负载排序干扰。 */
	std::optional<std::string> ReadCount(const std::string&) override {
		return "0";
	}

	/** 按故障模式失败或在锁内保存 Token。 */
	bool PutToken(int uid, const std::string& token) override {
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

	/** 读取用户 Token，缺失时返回空。 */
	std::optional<std::string> GetToken(int uid) override {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = tokens_.find(uid);
		return found == tokens_.end() ? std::nullopt : std::optional<std::string>(found->second);
	}

	/** 在锁内预置用户 Token 供校验场景使用。 */
	void Seed(int uid, std::string token) {
		std::lock_guard<std::mutex> lock(mutex_);
		tokens_[uid] = std::move(token);
	}

private:
	PutBehavior put_behavior_;
	std::mutex mutex_;
	std::unordered_map<int, std::string> tokens_;
};

/** 提供确定性的测试 Token 生成器。 */
class SyntheticTokenSource final : public status_routing_internal::TokenSource {
public:
	/** 返回固定测试 Token。 */
	std::string Next() override {
		return "SYNTHETIC_STATUS_TOKEN_3A02";
	}
};

// T08-STATUS-09
/** 验证 Token 写入返回失败时清空选服结果。 */
TEST(StatusRoutingComponentTests, StoreFalseFailsClosedAndClearsAssignment) {
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "9001"}},
		std::make_shared<InMemoryStatusStore>(InMemoryStatusStore::PutBehavior::ReturnFalse),
		std::make_shared<SyntheticTokenSource>());

	const auto result = routing->Assign(51);
	EXPECT_EQ(result.error, kRpcFailed);
	EXPECT_TRUE(result.host.empty());
	EXPECT_TRUE(result.port.empty());
	EXPECT_TRUE(result.token.empty());
}

// T08-STATUS-08
/** 验证写入成功时返回完整端点与 Token。 */
TEST(StatusRoutingComponentTests, SuccessfulStoreProducesCompleteAssignment) {
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "9001"}},
		std::make_shared<InMemoryStatusStore>(),
		std::make_shared<SyntheticTokenSource>());
	const auto result = routing->Assign(52);
	EXPECT_EQ(result.error, kSuccess);
	EXPECT_FALSE(result.host.empty());
	EXPECT_FALSE(result.port.empty());
	EXPECT_FALSE(result.token.empty());
}

// T08-STATUS-10
/** 验证存储异常被转换为稳定的失败响应。 */
TEST(StatusRoutingComponentTests, StoreExceptionUsesStableFailClosedEnvelope) {
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "9001"}},
		std::make_shared<InMemoryStatusStore>(InMemoryStatusStore::PutBehavior::Throw),
		std::make_shared<SyntheticTokenSource>());
	const auto result = routing->Assign(53);
	EXPECT_EQ(result.error, kRpcFailed);
	EXPECT_TRUE(result.host.empty());
	EXPECT_TRUE(result.port.empty());
	EXPECT_TRUE(result.token.empty());
}

// T08-STATUS-11
/** 验证用户不存在与 Token 不匹配使用不同业务错误。 */
TEST(StatusRoutingComponentTests, MissingUidIsDistinctFromMismatch) {
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "9001"}},
		std::make_shared<InMemoryStatusStore>(),
		std::make_shared<SyntheticTokenSource>());
	const auto result = routing->Validate(54, "candidate");
	EXPECT_EQ(result.error, kUidInvalid);
	EXPECT_EQ(result.uid, 0);
	EXPECT_TRUE(result.token.empty());
}

// T08-STATUS-12
/** 验证 Token 不匹配时拒绝认证并清空响应身份。 */
TEST(StatusRoutingComponentTests, TokenMismatchIsRejected) {
	auto store = std::make_shared<InMemoryStatusStore>();
	store->Seed(55, "stored-token");
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "9001"}}, store, std::make_shared<SyntheticTokenSource>());
	const auto result = routing->Validate(55, "candidate-token");
	EXPECT_EQ(result.error, kTokenInvalid);
	EXPECT_EQ(result.uid, 0);
	EXPECT_TRUE(result.token.empty());
}

// T08-STATUS-13
/** 验证匹配 Token 成功并返回用户身份。 */
TEST(StatusRoutingComponentTests, MatchingTokenSucceeds) {
	auto store = std::make_shared<InMemoryStatusStore>();
	store->Seed(56, "matching-token");
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "9001"}}, store, std::make_shared<SyntheticTokenSource>());
	const auto result = routing->Validate(56, "matching-token");
	EXPECT_EQ(result.error, kSuccess);
	EXPECT_EQ(result.uid, 56);
	EXPECT_EQ(result.token, "matching-token");
}

} // namespace
