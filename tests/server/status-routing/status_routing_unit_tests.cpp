#include <gtest/gtest.h>

#include "StatusRouting.h"
#include "StatusRoutingInternal.h"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kSuccess = 0;
constexpr int kRpcFailed = 1002;

class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	explicit InMemoryStatusStore(
		std::unordered_map<std::string, std::optional<std::string>> counts = {})
		: counts_(std::move(counts)) {
	}

	std::optional<std::string> ReadCount(const std::string& name) override {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = counts_.find(name);
		return found == counts_.end() ? std::nullopt : found->second;
	}

	bool PutToken(int uid, const std::string& token) override {
		std::lock_guard<std::mutex> lock(mutex_);
		tokens_[uid] = token;
		return true;
	}

	std::optional<std::string> GetToken(int uid) override {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = tokens_.find(uid);
		return found == tokens_.end() ? std::nullopt : std::optional<std::string>(found->second);
	}

private:
	std::mutex mutex_;
	std::unordered_map<std::string, std::optional<std::string>> counts_;
	std::unordered_map<int, std::string> tokens_;
};

class FixedTokenSource final : public status_routing_internal::TokenSource {
public:
	std::string Next() override {
		return "SYNTHETIC_STATUS_TOKEN_3A02";
	}
};

// T08-STATUS-01
TEST(StatusRoutingUnitTests, EmptyServerListFailsClosed) {
	auto routing = status_routing_internal::CreateStatusRouting(
		{},
		std::make_shared<InMemoryStatusStore>(),
		std::make_shared<FixedTokenSource>());

	const auto result = routing->Assign(41);
	EXPECT_EQ(result.error, kRpcFailed);
	EXPECT_TRUE(result.host.empty());
	EXPECT_TRUE(result.port.empty());
	EXPECT_TRUE(result.token.empty());
}

AssignmentResult AssignWithCounts(
	std::vector<RoutingServer> servers,
	std::unordered_map<std::string, std::optional<std::string>> counts) {
	auto routing = status_routing_internal::CreateStatusRouting(
		std::move(servers),
		std::make_shared<InMemoryStatusStore>(std::move(counts)),
		std::make_shared<FixedTokenSource>());
	return routing->Assign(42);
}

// T08-STATUS-02
TEST(StatusRoutingUnitTests, SingleServerIsSelected) {
	const auto result = AssignWithCounts({{"chat-z", "127.0.0.1", "9001"}}, {{"chat-z", "4"}});
	EXPECT_EQ(result.error, kSuccess);
	EXPECT_EQ(result.host, "127.0.0.1");
	EXPECT_EQ(result.port, "9001");
}

// T08-STATUS-03
TEST(StatusRoutingUnitTests, LowestValidConnectionCountWins) {
	const auto result = AssignWithCounts(
		{{"chat-b", "10.0.0.2", "9002"}, {"chat-a", "10.0.0.1", "9001"}},
		{{"chat-a", "8"}, {"chat-b", "2"}});
	EXPECT_EQ(result.host, "10.0.0.2");
	EXPECT_EQ(result.port, "9002");
}

// T08-STATUS-04
TEST(StatusRoutingUnitTests, EqualValidCountsUseRuntimeNameOrder) {
	const auto result = AssignWithCounts(
		{{"chat-z", "10.0.0.9", "9009"}, {"chat-a", "10.0.0.1", "9001"}},
		{{"chat-z", "3"}, {"chat-a", "3"}});
	EXPECT_EQ(result.host, "10.0.0.1");
}

// T08-STATUS-05
TEST(StatusRoutingUnitTests, ValidCountRanksBeforeUnknownCount) {
	const auto result = AssignWithCounts(
		{{"chat-a", "10.0.0.1", "9001"}, {"chat-z", "10.0.0.9", "9009"}},
		{{"chat-z", "100"}});
	EXPECT_EQ(result.host, "10.0.0.9");
}

// T08-STATUS-06
TEST(StatusRoutingUnitTests, AllUnknownCountsUseRuntimeNameOrder) {
	const auto result = AssignWithCounts(
		{{"chat-z", "10.0.0.9", "9009"}, {"chat-a", "10.0.0.1", "9001"}},
		{{"chat-z", std::nullopt}, {"chat-a", ""}});
	EXPECT_EQ(result.host, "10.0.0.1");
}

// T08-STATUS-07
TEST(StatusRoutingUnitTests, NegativeAndMalformedCountsAreUnknown) {
	const auto result = AssignWithCounts(
		{{"chat-z", "10.0.0.9", "9009"}, {"chat-a", "10.0.0.1", "9001"}, {"chat-m", "10.0.0.5", "9005"}},
		{{"chat-z", "-1"}, {"chat-a", "12x"}, {"chat-m", "7"}});
	EXPECT_EQ(result.host, "10.0.0.5");
}

// T08-STATUS-14
TEST(StatusRoutingUnitTests, ConcurrentSelectionIsDeterministic) {
	constexpr int thread_count = 16;
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-z", "10.0.0.9", "9009"}, {"chat-a", "10.0.0.1", "9001"}},
		std::make_shared<InMemoryStatusStore>(
			std::unordered_map<std::string, std::optional<std::string>>{{"chat-z", "4"}, {"chat-a", "4"}}),
		std::make_shared<FixedTokenSource>());
	std::mutex mutex;
	std::condition_variable condition;
	bool start = false;
	std::vector<std::string> hosts(thread_count);
	std::vector<std::thread> threads;
	for (int index = 0; index < thread_count; ++index) {
		threads.emplace_back([&, index]() {
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, [&]() { return start; });
			}
			hosts[static_cast<std::size_t>(index)] = routing->Assign(1000 + index).host;
		});
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		start = true;
	}
	condition.notify_all();
	for (auto& thread : threads) {
		thread.join();
	}
	EXPECT_TRUE(std::all_of(hosts.begin(), hosts.end(), [](const std::string& host) {
		return host == "10.0.0.1";
	}));
}

} // namespace
