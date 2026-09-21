#include <gtest/gtest.h>

#include "RunContext.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;

// T09-PROC-01
TEST(T09_PROC_RunContext, IdentityAndSyntheticNamespaceAreImmutableAndUnique) {
	auto first = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	auto second = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);

	EXPECT_FALSE(first->RunId().empty());
	EXPECT_NE(first->RunId(), second->RunId());
	EXPECT_NE(first->SyntheticIdentity("user"), second->SyntheticIdentity("user"));
	EXPECT_EQ(first->SyntheticIdentity("user"), first->RunId() + "-user");
}

// T09-PROC-02
TEST(T09_PROC_RunContext, AllocatesDistinctOwnedLoopbackPortsAndRejectsDuplicateNames) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);

	const auto gate = context->ReserveLoopbackPort("gate");
	const auto status = context->ReserveLoopbackPort("status");

	EXPECT_EQ(gate.address, "127.0.0.1");
	EXPECT_EQ(status.address, "127.0.0.1");
	EXPECT_NE(gate.port, 0);
	EXPECT_NE(status.port, 0);
	EXPECT_NE(gate.port, status.port);
	EXPECT_THROW(context->ReserveLoopbackPort("gate"), std::invalid_argument);
}

// T09-PROC-03
TEST(T09_PROC_RunContext, CreatesOwnedTempRootAndRefusesOutsideOrPreexistingPaths) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto root = context->TempRoot();
	ASSERT_TRUE(std::filesystem::is_directory(root));

	const auto owned = context->CreateOwnedDirectory("evidence");
	ASSERT_TRUE(std::filesystem::is_directory(owned));
	EXPECT_THROW(context->CreateOwnedDirectory("evidence"), std::invalid_argument);
	EXPECT_THROW(context->CreateOwnedDirectory("..\\outside"), std::invalid_argument);
	EXPECT_FALSE(context->CleanupOwnedPath(std::filesystem::temp_directory_path()));
}

// T09-PROC-04
TEST(T09_PROC_RunContext, RequiresFutureAbsoluteDeadlineAndCommittedResourceOwnership) {
	EXPECT_THROW(
		integration::RunContext::Create(std::chrono::steady_clock::time_point::max()),
		std::invalid_argument);
	EXPECT_THROW(
		integration::RunContext::Create(std::chrono::steady_clock::now() - 1ms),
		std::invalid_argument);

	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto slot = context->ReserveProcessSlot("helper");
	integration::ProcessIdentity identity{4242, 123456};
	EXPECT_FALSE(context->IsOwnedProcess(identity));
	context->CommitProcess(slot, identity, [] { return integration::CleanupStatus::Success("stopped"); });
	EXPECT_TRUE(context->IsOwnedProcess(identity));
	EXPECT_FALSE(context->CleanupProcess(integration::ProcessIdentity{4242, 123457}));
}

// T09-PROC-05
TEST(T09_PROC_RunContext, TeardownIsReverseOrderedAndPreservesPrimaryAndCleanupFailures) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	std::vector<std::string> order;
	const auto first = context->ReserveProcessSlot("first");
	const auto second = context->ReserveProcessSlot("second");
	context->CommitProcess(first, {1001, 11}, [&] {
		order.push_back("first");
		return integration::CleanupStatus::Success("first stopped");
	});
	context->CommitProcess(second, {1002, 12}, [&] {
		order.push_back("second");
		return integration::CleanupStatus::Failure("cleanup marker");
	});
	context->RecordPrimaryFailure("primary marker");

	const auto result = context->Teardown();

	EXPECT_EQ(order, (std::vector<std::string>{"second", "first"}));
	ASSERT_TRUE(result.primary_failure.has_value());
	EXPECT_EQ(*result.primary_failure, "primary marker");
	ASSERT_EQ(result.cleanup_failures.size(), 1U);
	EXPECT_EQ(result.cleanup_failures.front(), "cleanup marker");
	EXPECT_FALSE(result.complete);
	EXPECT_FALSE(context->CleanupProcess({1002, 12}));
}

} // namespace
