#include <gtest/gtest.h>

#include "SessionLifecycleCoordinator.h"
#include "../chat-session-state/session_test_support.h"
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

/** 提供不发送邮件的验证码服务替身，供宿主装配合同使用。 */
class InMemoryVerification final : public gate::internal::VerificationPort {
public:
	/** 返回验证码请求成功，不执行外部操作。 */
	int RequestCode(const std::string&) override {
		return 0;
	}
};

/** 提供始终缺失验证码的存储替身。 */
class InMemoryCodeStore final : public gate::internal::CodeStore {
public:
	/** 返回空验证码，避免依赖真实 Redis。 */
	std::optional<std::string> ReadCode(const std::string&) override {
		return std::nullopt;
	}
};

/** 提供拒绝用户操作的存储替身，仅用于模块装配。 */
class InMemoryUserStore final : public gate::internal::UserStore {
public:
	/** 返回未创建用户，不写入外部数据库。 */
	int CreateUser(const std::string&, const std::string&, const std::string&) override {
		return 0;
	}

	/** 拒绝身份匹配，避免替身模拟真实认证成功。 */
	bool IdentityMatches(const std::string&, const std::string&) override {
		return false;
	}

	/** 拒绝密码更新，不产生持久化副作用。 */
	bool UpdatePassword(const std::string&, const std::string&) override {
		return false;
	}

	/** 返回凭据不匹配，不生成测试用户记录。 */
	std::optional<gate::internal::UserRecord> CheckCredentials(
		const std::string&, const std::string&) override {
		return std::nullopt;
	}
};

/** 提供默认失败的选服端口替身。 */
class InMemoryStatusPort final : public gate::internal::StatusPort {
public:
	/** 返回默认分配结果，不发起外部 RPC。 */
	gate::internal::StatusAssignment Assign(int) override {
		return {};
	}
};

/** 提供最小 Status 存储接口替身，隔离真实 Redis。 */
class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	/** 返回缺失负载，供默认路由装配使用。 */
	std::optional<std::string> ReadCount(const std::string&) override {
		return std::nullopt;
	}

	/** 接受 Token 写入但不持久化，仅满足宿主装配接口。 */
	bool PutToken(int, const std::string&) override {
		return true;
	}

	/** 返回缺失 Token，不模拟真实登录成功。 */
	std::optional<std::string> GetToken(int) override {
		return std::nullopt;
	}
};

/** 提供可重复的测试 Token 来源。 */
class FixedTokenSource final : public status_routing_internal::TokenSource {
public:
	/** 返回宿主合同专用的合成 Token。 */
	std::string Next() override {
		return "host-contract-token";
	}
};

/** 组装真实生产模块及外部端口替身，供宿主生命周期测试。 */
integration::ProductionModules CreateProductionModules() {
	integration::ProductionModules modules;
	modules.logic_dispatcher = std::make_unique<LogicDispatcher>(
		/** 接受逻辑消息且不产生外部副作用。 */ [](const LogicMessage&) { return true; });
	modules.chat_sessions = std::make_unique<SessionLifecycleCoordinator>(
        std::make_shared<UserSessionDirectory>(), std::make_shared<session_test::MemoryPresence>(), "host-contract");
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

/** 共享记录传输就绪、停止次数及截止时间。 */
struct TransportState {
	std::atomic<int> ready_calls{0};
	std::atomic<int> stop_calls{0};
	integration::Deadline last_stop_deadline{};
};

/** 实现可观测的传输替身，通过共享状态验证宿主委托。 */
class ContractTransport final : public integration::TransportHost {
public:
	/** 保存绑定端点与共享观察状态。 */
	ContractTransport(
		integration::LoopbackEndpoint bound,
		std::shared_ptr<TransportState> state)
		: bound_(std::move(bound)), state_(std::move(state)) {
	}

	/** 返回传输实际绑定端点的副本。 */
	integration::LoopbackEndpoint BoundEndpoint() const override {
		return bound_;
	}

	/** 记录就绪检查次数，仅在期限未过时报告就绪。 */
	bool WaitReady(integration::Deadline deadline) override {
		++state_->ready_calls;
		return deadline > std::chrono::steady_clock::now();
	}

	/** 记录停止次数和传入截止时间，并返回期限是否有效。 */
	integration::CleanupResult Stop(integration::Deadline deadline) override {
		++state_->stop_calls;
		state_->last_stop_deadline = deadline;
		return {deadline > std::chrono::steady_clock::now(), "transport stopped"};
	}

private:
	integration::LoopbackEndpoint bound_;
	std::shared_ptr<TransportState> state_;
};

/** 建立隔离宿主规格，注入生产模块、传输替身与清理观察者。 */
integration::HostSpec MakeSpec(
	std::shared_ptr<TransportState> state,
	std::shared_ptr<integration::CleanupObserver> cleanup =
		std::make_shared<integration::CleanupObserver>()) {
	integration::HostSpec spec;
	spec.family = integration::HostFamily::GateHttp;
	spec.endpoint = {"127.0.0.1", 0};
	spec.deadline = std::chrono::steady_clock::now() + 5s;
	spec.create_modules = CreateProductionModules;
	spec.create_transport = /** 创建固定 loopback 端点的传输替身并共享调用记录。 */ [state = std::move(state)](
		const integration::LoopbackEndpoint&,
		const integration::ProductionModules&) {
		return std::make_unique<ContractTransport>(
			integration::LoopbackEndpoint{"127.0.0.1", 43123}, state);
	};
	spec.cleanup = std::move(cleanup);
	return spec;
}

// T09-HOST-01
/** 验证宿主拒绝非 loopback 端点。 */
TEST(T09_HOST_Contract, RejectsNonLoopbackEndpoint) {
	auto spec = MakeSpec(std::make_shared<TransportState>());
	spec.endpoint.address = "192.0.2.1";
	EXPECT_THROW(integration::IntegrationHostFactory::Start(std::move(spec)), std::invalid_argument);
}

// T09-HOST-02
/** 验证所选宿主缺少对应生产模块时拒绝启动。 */
TEST(T09_HOST_Contract, RequiresSelectedConcreteProductionModule) {
	auto spec = MakeSpec(std::make_shared<TransportState>());
	spec.create_modules = /** 移除 Gate 模块以注入装配缺项。 */ []() {
		auto modules = CreateProductionModules();
		modules.gate_request.reset();
		return modules;
	};
	EXPECT_THROW(integration::IntegrationHostFactory::Start(std::move(spec)), std::invalid_argument);
}

// T09-HOST-03
/** 验证宿主拒绝已经过期的运行截止时间。 */
TEST(T09_HOST_Contract, RejectsExpiredOwnedDeadline) {
	auto spec = MakeSpec(std::make_shared<TransportState>());
	spec.deadline = std::chrono::steady_clock::now() - 1ms;
	EXPECT_THROW(integration::IntegrationHostFactory::Start(std::move(spec)), std::invalid_argument);
}

// T09-HOST-04
/** 验证宿主公开实际绑定端点，并执行真实传输就绪委托。 */
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
/** 验证停止有界且幂等，重复调用保留首个停止结果。 */
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
/** 验证析构发布清理结果，且传输只停止一次。 */
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
