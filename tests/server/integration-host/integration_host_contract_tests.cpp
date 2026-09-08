#include <gtest/gtest.h>

#include "ChatSessionState.h"
#include "ChatSessionStateInternal.h"
#include "GateRequestInternal.h"
#include "IntegrationHostFactory.h"
#include "LogicDispatcher.h"
#include "StatusRoutingInternal.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

class FixedSessionIdSource final : public SessionIdSource {
public:
	std::string Next() override {
		return "host-contract-session";
	}
};

class InMemoryPresence final : public SessionPresence {
public:
	void Register(int, const std::string&) override {
	}

	void Cleanup(int, const std::string&) override {
	}
};

class InMemoryVerification final : public gate::internal::VerificationPort {
public:
	int RequestCode(const std::string&) override {
		return 0;
	}
};

class InMemoryCodeStore final : public gate::internal::CodeStore {
public:
	std::optional<std::string> ReadCode(const std::string&) override {
		return std::nullopt;
	}
};

class InMemoryUserStore final : public gate::internal::UserStore {
public:
	int CreateUser(const std::string&, const std::string&, const std::string&) override {
		return 0;
	}

	bool IdentityMatches(const std::string&, const std::string&) override {
		return false;
	}

	bool UpdatePassword(const std::string&, const std::string&) override {
		return false;
	}

	std::optional<gate::internal::UserRecord> CheckCredentials(
		const std::string&, const std::string&) override {
		return std::nullopt;
	}
};

class InMemoryStatusPort final : public gate::internal::StatusPort {
public:
	gate::internal::StatusAssignment Assign(int) override {
		return {};
	}
};

class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	std::optional<std::string> ReadCount(const std::string&) override {
		return std::nullopt;
	}

	bool PutToken(int, const std::string&) override {
		return true;
	}

	std::optional<std::string> GetToken(int) override {
		return std::nullopt;
	}
};

class FixedTokenSource final : public status_routing_internal::TokenSource {
public:
	std::string Next() override {
		return "host-contract-token";
	}
};

integration::ProductionModules CreateProductionModules() {
	integration::ProductionModules modules;
	modules.logic_dispatcher = std::make_unique<LogicDispatcher>(
		[](const LogicMessage&) { return true; });
	modules.chat_sessions = std::make_unique<ChatSessionState>(
		std::make_shared<FixedSessionIdSource>(),
		std::make_shared<InMemoryPresence>());
	modules.gate_request = gate::internal::CreateGateRequest(
		std::make_shared<InMemoryVerification>(),
		std::make_shared<InMemoryCodeStore>(),
		std::make_shared<InMemoryUserStore>(),
		std::make_shared<InMemoryStatusPort>());
	modules.status_routing = status_routing_internal::CreateStatusRouting(
		std::vector<RoutingServer>{},
		std::make_shared<InMemoryStatusStore>(),
		std::make_shared<FixedTokenSource>());
	return modules;
}

struct TransportState {
	std::atomic<int> ready_calls{0};
	std::atomic<int> stop_calls{0};
	integration::Deadline last_stop_deadline{};
};

class ContractTransport final : public integration::TransportHost {
public:
	ContractTransport(
		integration::LoopbackEndpoint bound,
		std::shared_ptr<TransportState> state)
		: bound_(std::move(bound)), state_(std::move(state)) {
	}

	integration::LoopbackEndpoint BoundEndpoint() const override {
		return bound_;
	}

	bool WaitReady(integration::Deadline deadline) override {
		++state_->ready_calls;
		return deadline > std::chrono::steady_clock::now();
	}

	integration::CleanupResult Stop(integration::Deadline deadline) override {
		++state_->stop_calls;
		state_->last_stop_deadline = deadline;
		return {deadline > std::chrono::steady_clock::now(), "transport stopped"};
	}

private:
	integration::LoopbackEndpoint bound_;
	std::shared_ptr<TransportState> state_;
};

integration::HostSpec MakeSpec(
	std::shared_ptr<TransportState> state,
	std::shared_ptr<integration::CleanupObserver> cleanup =
		std::make_shared<integration::CleanupObserver>()) {
	integration::HostSpec spec;
	spec.family = integration::HostFamily::GateHttp;
	spec.endpoint = {"127.0.0.1", 0};
	spec.deadline = std::chrono::steady_clock::now() + 5s;
	spec.create_modules = CreateProductionModules;
	spec.create_transport = [state = std::move(state)](
		const integration::LoopbackEndpoint&,
		const integration::ProductionModules&) {
		return std::make_unique<ContractTransport>(
			integration::LoopbackEndpoint{"127.0.0.1", 43123}, state);
	};
	spec.cleanup = std::move(cleanup);
	return spec;
}

// T09-HOST-01
TEST(T09_HOST_Contract, RejectsNonLoopbackEndpoint) {
	auto spec = MakeSpec(std::make_shared<TransportState>());
	spec.endpoint.address = "192.0.2.1";
	EXPECT_THROW(integration::IntegrationHostFactory::Start(std::move(spec)), std::invalid_argument);
}

// T09-HOST-02
TEST(T09_HOST_Contract, RequiresSelectedConcreteProductionModule) {
	auto spec = MakeSpec(std::make_shared<TransportState>());
	spec.create_modules = []() {
		auto modules = CreateProductionModules();
		modules.gate_request.reset();
		return modules;
	};
	EXPECT_THROW(integration::IntegrationHostFactory::Start(std::move(spec)), std::invalid_argument);
}

// T09-HOST-03
TEST(T09_HOST_Contract, RejectsExpiredOwnedDeadline) {
	auto spec = MakeSpec(std::make_shared<TransportState>());
	spec.deadline = std::chrono::steady_clock::now() - 1ms;
	EXPECT_THROW(integration::IntegrationHostFactory::Start(std::move(spec)), std::invalid_argument);
}

// T09-HOST-04
TEST(T09_HOST_Contract, ExposesActualBoundEndpointAndReadyProbe) {
	auto state = std::make_shared<TransportState>();
	auto host = integration::IntegrationHostFactory::Start(MakeSpec(state));

	const auto bound = host->BoundEndpoint();
	EXPECT_EQ(bound.address, "127.0.0.1");
	EXPECT_EQ(bound.port, 43123);
	EXPECT_TRUE(host->Ready());
	EXPECT_GE(state->ready_calls.load(), 2);
}

// T09-HOST-05
TEST(T09_HOST_Contract, StopIsBoundedAndIdempotent) {
	auto state = std::make_shared<TransportState>();
	auto host = integration::IntegrationHostFactory::Start(MakeSpec(state));
	const auto deadline = std::chrono::steady_clock::now() + 1s;

	const auto first = host->Stop(deadline);
	const auto second = host->Stop(std::chrono::steady_clock::now() - 1ms);

	EXPECT_TRUE(first.complete);
	EXPECT_EQ(second.complete, first.complete);
	EXPECT_EQ(second.detail, first.detail);
	EXPECT_EQ(state->stop_calls.load(), 1);
	EXPECT_EQ(state->last_stop_deadline, deadline);
}

// T09-HOST-06
TEST(T09_HOST_Contract, DestructionPublishesBoundedCleanupResult) {
	auto state = std::make_shared<TransportState>();
	auto cleanup = std::make_shared<integration::CleanupObserver>();
	{
		auto host = integration::IntegrationHostFactory::Start(MakeSpec(state, cleanup));
		EXPECT_FALSE(cleanup->Published());
	}

	EXPECT_TRUE(cleanup->Published());
	EXPECT_TRUE(cleanup->Result().complete);
	EXPECT_EQ(state->stop_calls.load(), 1);
}

} // namespace
