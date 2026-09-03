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

class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	enum class PutBehavior { Succeed, ReturnFalse, Throw };

	explicit InMemoryStatusStore(PutBehavior put_behavior = PutBehavior::Succeed)
		: put_behavior_(put_behavior) {
	}

	std::optional<std::string> ReadCount(const std::string&) override {
		return "0";
	}

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

	std::optional<std::string> GetToken(int uid) override {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = tokens_.find(uid);
		return found == tokens_.end() ? std::nullopt : std::optional<std::string>(found->second);
	}

	void Seed(int uid, std::string token) {
		std::lock_guard<std::mutex> lock(mutex_);
		tokens_[uid] = std::move(token);
	}

private:
	PutBehavior put_behavior_;
	std::mutex mutex_;
	std::unordered_map<int, std::string> tokens_;
};

class SyntheticTokenSource final : public status_routing_internal::TokenSource {
public:
	std::string Next() override {
		return "SYNTHETIC_STATUS_TOKEN_3A02";
	}
};

// T08-STATUS-09
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
