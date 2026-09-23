#include <gtest/gtest.h>

#include "../../../StatusServer/StatusServer/StatusGrpcServer.h"
#include "../../../StatusServer/StatusServer/StatusRoutingInternal.h"
#include "../../../common/grpc/GrpcClientRuntime.h"
#include "../../../generated/proto/cpp/status.grpc.pb.h"
#include "../../support/RunContext.h"

#include <chrono>
#include <condition_variable>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr int kSuccess = 0;
constexpr int kRpcFailed = 1002;
constexpr int kUidInvalid = 1010;

/** 线程安全存储测试负载与 Token，可阻塞下一次读取以注入 RPC 故障。 */
class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	/** 在可选故障屏障后读取指定实例负载，缺失时返回空值。 */
	std::optional<std::string> ReadCount(const std::string& name) override {
		{
			std::unique_lock<std::mutex> lock(fault_mutex_);
			if (block_next_read_) {
				block_next_read_ = false;
				read_blocked_ = true;
				fault_condition_.notify_all();
				fault_condition_.wait(lock, /** 等待测试线程允许被阻塞的读取继续。 */ [this] { return release_read_; });
				read_completed_ = true;
				fault_condition_.notify_all();
			}
		}
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = counts_.find(name);
		return found == counts_.end() ? std::nullopt : std::optional<std::string>(found->second);
	}

	/** 在锁内保存用户 Token 并报告成功。 */
	bool PutToken(int uid, const std::string& token) override {
		std::lock_guard<std::mutex> lock(mutex_);
		tokens_[uid] = token;
		return true;
	}

	/** 在锁内读取用户 Token，缺失时返回空值。 */
	std::optional<std::string> GetToken(int uid) override {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = tokens_.find(uid);
		return found == tokens_.end() ? std::nullopt : std::optional<std::string>(found->second);
	}

	/** 在锁内设置指定实例的测试负载。 */
	void SetCount(std::string name, std::string count) {
		std::lock_guard<std::mutex> lock(mutex_);
		counts_[std::move(name)] = std::move(count);
	}

	/** 重置故障状态，使下一次负载读取停在测试屏障。 */
	void ScheduleReadBlock() {
		std::lock_guard<std::mutex> lock(fault_mutex_);
		block_next_read_ = true;
		read_blocked_ = false;
		release_read_ = false;
		read_completed_ = false;
	}

	/** 等待故障读取进入屏障，截止后返回失败。 */
	bool WaitForReadBlocked(std::chrono::steady_clock::time_point deadline) {
		std::unique_lock<std::mutex> lock(fault_mutex_);
		return fault_condition_.wait_until(lock, deadline, /** 检查读取是否已进入阻塞状态。 */ [this] { return read_blocked_; });
	}

	/** 释放读取屏障并通知等待线程。 */
	void ReleaseRead() {
		{
			std::lock_guard<std::mutex> lock(fault_mutex_);
			release_read_ = true;
		}
		fault_condition_.notify_all();
	}

	/** 等待故障读取越过屏障，截止后返回失败。 */
	bool WaitForReadCompleted(std::chrono::steady_clock::time_point deadline) {
		std::unique_lock<std::mutex> lock(fault_mutex_);
		return fault_condition_.wait_until(lock, deadline, /** 检查被阻塞的读取是否已恢复完成。 */ [this] { return read_completed_; });
	}

private:
	std::mutex mutex_;
	std::unordered_map<std::string, std::string> counts_;
	std::unordered_map<int, std::string> tokens_;
	std::mutex fault_mutex_;
	std::condition_variable fault_condition_;
	bool block_next_read_ = false;
	bool read_blocked_ = false;
	bool release_read_ = false;
	bool read_completed_ = false;
};

/** 提供固定合成 Token，使协议断言不依赖随机值。 */
class FixedTokenSource final : public status_routing_internal::TokenSource {
public:
	/** 返回测试专用的固定合成 Token。 */
	std::string Next() override {
		return "SYNTHETIC_STATUS_GRPC_TOKEN";
	}
};

/** 拥有真实 Status gRPC 服务、生成桩及内存存储，隔离外部 Redis。 */
class T09_SGRPC_Core : public testing::Test {
protected:
	/** 组装测试路由并启动真实 loopback gRPC 服务和客户端桩。 */
	void SetUp() override {
		store_ = std::make_shared<InMemoryStatusStore>();
		store_->SetCount("chat-a", "0");
		routing_ = status_routing_internal::CreateStatusRouting(
			{{"chat-a", "127.0.0.1", "29001"}}, store_, std::make_shared<FixedTokenSource>());
		server_ = std::make_unique<status::StatusGrpcServer>(*routing_);
		ASSERT_TRUE(server_->Start("127.0.0.1:0"));
		channel_ = grpc::CreateChannel(server_->BoundEndpoint(), grpc::InsecureChannelCredentials());
		stub_ = message::StatusService::NewStub(channel_);
	}

	/** 在两秒截止时间内停止本夹具服务。 */
	void TearDown() override {
		if (server_) {
			EXPECT_TRUE(server_->Stop(std::chrono::system_clock::now() + 2s));
		}
	}

	/** 通过生成桩发起有界选服请求，响应写入调用者提供的对象。 */
	grpc::Status GetChatServer(int uid, message::GetChatServerRsp& response) {
		grpc::ClientContext context;
		context.set_deadline(std::chrono::system_clock::now() + 2s);
		message::GetChatServerReq request;
		request.set_uid(uid);
		return stub_->GetChatServer(&context, request, &response);
	}

	std::shared_ptr<InMemoryStatusStore> store_;
	std::unique_ptr<StatusRouting> routing_;
	std::unique_ptr<status::StatusGrpcServer> server_;
	std::shared_ptr<grpc::Channel> channel_;
	std::unique_ptr<message::StatusService::Stub> stub_;
};

// T09-SGRPC-01
/** 验证 gRPC 通道可连接，且服务发布数字 loopback 端点。 */
TEST_F(T09_SGRPC_Core, PublishesProtocolReadyNumericLoopbackEndpoint) {
	EXPECT_TRUE(channel_->WaitForConnected(std::chrono::system_clock::now() + 2s));
	EXPECT_EQ(server_->BoundAddress(), "127.0.0.1");
	EXPECT_NE(server_->BoundPort(), 0);
}

// T09-SGRPC-02 (also exercises the maximum valid int32 request field)
/** 验证生成选服桩把最大合法用户编号委托给路由。 */
TEST_F(T09_SGRPC_Core, GeneratedGetChatServerStubDelegatesMaximumUidToRouting) {
	message::GetChatServerRsp response;
	const auto status = GetChatServer((std::numeric_limits<std::int32_t>::max)(), response);
	ASSERT_TRUE(status.ok()) << status.error_message();
	EXPECT_EQ(response.error(), kSuccess);
	EXPECT_EQ(response.host(), "127.0.0.1");
	EXPECT_EQ(response.port(), "29001");
	EXPECT_EQ(response.token(), "SYNTHETIC_STATUS_GRPC_TOKEN");
}

// T09-SGRPC-03
/** 验证生成登录桩使用已存储 Token 完成认证。 */
TEST_F(T09_SGRPC_Core, GeneratedLoginStubDelegatesStoredTokenValidation) {
	message::GetChatServerRsp assignment;
	ASSERT_TRUE(GetChatServer(71, assignment).ok());

	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 2s);
	message::LoginReq request;
	request.set_uid(71);
	request.set_token(assignment.token());
	message::LoginRsp response;
	const auto status = stub_->Login(&context, request, &response);
	ASSERT_TRUE(status.ok()) << status.error_message();
	EXPECT_EQ(response.error(), kSuccess);
	EXPECT_EQ(response.uid(), 71);
	EXPECT_EQ(response.token(), assignment.token());
}

// T09-SGRPC-04
/** 验证空服务列表通过真实生成桩返回明确业务失败。 */
TEST(T09_SGRPC_CoreStandalone, EmptyServerListFailsClosedOverGeneratedStub) {
	auto routing = status_routing_internal::CreateStatusRouting(
		{}, std::make_shared<InMemoryStatusStore>(), std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	ASSERT_TRUE(server.Start("127.0.0.1:0"));
	auto channel = grpc::CreateChannel(server.BoundEndpoint(), grpc::InsecureChannelCredentials());
	auto stub = message::StatusService::NewStub(channel);
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 2s);
	message::GetChatServerReq request;
	request.set_uid(72);
	message::GetChatServerRsp response;
	const auto call = stub->GetChatServer(&context, request, &response);
	EXPECT_TRUE(call.ok()) << call.error_message();
	EXPECT_EQ(response.error(), kRpcFailed);
	EXPECT_TRUE(response.host().empty());
	EXPECT_TRUE(response.port().empty());
	EXPECT_TRUE(response.token().empty());
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
}

// T09-SGRPC-05
/** 验证非法业务输入使用稳定、脱敏且不泄漏 Token 的响应。 */
TEST_F(T09_SGRPC_Core, InvalidBusinessInputUsesStableSanitizedEnvelope) {
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 2s);
	message::LoginReq request;
	request.set_uid(0);
	request.set_token("SYNTHETIC_INVALID_TOKEN");
	message::LoginRsp response;
	const auto status = stub_->Login(&context, request, &response);
	ASSERT_TRUE(status.ok()) << status.error_message();
	EXPECT_EQ(response.error(), kUidInvalid);
	EXPECT_EQ(response.uid(), 0);
	EXPECT_TRUE(response.token().empty());
	EXPECT_TRUE(status.error_message().empty());
}

/** 为指定端点创建 Status 生成客户端桩。 */
std::unique_ptr<message::StatusService::Stub> MakeStub(const std::string& endpoint) {
	return message::StatusService::NewStub(
		grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
}

/** 使用调用者的上下文发起选服，保留其截止和取消设置。 */
grpc::Status InvokeAssignment(
	message::StatusService::Stub& stub,
	grpc::ClientContext& context,
	int uid,
	message::GetChatServerRsp& response) {
	message::GetChatServerReq request;
	request.set_uid(uid);
	return stub.GetChatServer(&context, request, &response);
}

// T09-SGRPC-06
/** 验证阻塞选服在截止到期后有界结束，并分类为超时。 */
TEST(T09_SGRPC_Fault, DeadlineExpiryIsBoundedAndClassified) {
	auto store = std::make_shared<InMemoryStatusStore>();
	store->SetCount("chat-a", "0");
	store->ScheduleReadBlock();
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "29001"}}, store, std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	ASSERT_TRUE(server.Start("127.0.0.1:0"));
	auto stub = MakeStub(server.BoundEndpoint());
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 100ms);
	message::GetChatServerRsp response;
	auto call = std::async(std::launch::async, /** 在独立任务中发起将被存储屏障阻塞的选服请求。 */ [&] {
		return InvokeAssignment(*stub, context, 106, response);
	});

	const bool entered = store->WaitForReadBlocked(std::chrono::steady_clock::now() + 1s);
	const bool completed = call.wait_until(std::chrono::steady_clock::now() + 1s) == std::future_status::ready;
	store->ReleaseRead();
	ASSERT_TRUE(entered);
	ASSERT_TRUE(completed);
	const auto result = call.get();
	EXPECT_EQ(rpc::ClassifyStatus(result), rpc::Failure::DeadlineExceeded);
	EXPECT_TRUE(result.error_message().empty() || result.error_message().find("SYNTHETIC") == std::string::npos);
	EXPECT_TRUE(store->WaitForReadCompleted(std::chrono::steady_clock::now() + 1s));
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
}

// T09-SGRPC-07
/** 验证显式取消阻塞 RPC 只产生一个终态。 */
TEST(T09_SGRPC_Fault, ExplicitCancellationHasOneTerminalOutcome) {
	auto store = std::make_shared<InMemoryStatusStore>();
	store->SetCount("chat-a", "0");
	store->ScheduleReadBlock();
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "29001"}}, store, std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	ASSERT_TRUE(server.Start("127.0.0.1:0"));
	auto stub = MakeStub(server.BoundEndpoint());
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 2s);
	message::GetChatServerRsp response;
	auto call = std::async(std::launch::async, /** 异步发起选服，供主测试线程显式取消。 */ [&] {
		return InvokeAssignment(*stub, context, 107, response);
	});

	const bool entered = store->WaitForReadBlocked(std::chrono::steady_clock::now() + 1s);
	context.TryCancel();
	const bool completed = call.wait_until(std::chrono::steady_clock::now() + 1s) == std::future_status::ready;
	store->ReleaseRead();
	ASSERT_TRUE(entered);
	ASSERT_TRUE(completed);
	EXPECT_EQ(rpc::ClassifyStatus(call.get()), rpc::Failure::Cancelled);
	EXPECT_TRUE(store->WaitForReadCompleted(std::chrono::steady_clock::now() + 1s));
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
}

// T09-SGRPC-08
/** 验证拒连 RPC 在期限内失败且错误输出脱敏。 */
TEST(T09_SGRPC_Fault, RefusedConnectionIsBoundedAndSanitized) {
	auto run = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = run->ReserveLoopbackPort("status-refused");
	ASSERT_TRUE(run->ReleaseLoopbackPort("status-refused"));
	auto stub = MakeStub(endpoint.address + ":" + std::to_string(endpoint.port));
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 500ms);
	message::GetChatServerRsp response;
	const auto started = std::chrono::steady_clock::now();
	const auto result = InvokeAssignment(*stub, context, 108, response);
	EXPECT_EQ(rpc::ClassifyStatus(result), rpc::Failure::DeadlineExceeded);
	EXPECT_LE(std::chrono::steady_clock::now() - started, 700ms);
	EXPECT_TRUE(result.error_message().find("SYNTHETIC") == std::string::npos);
	EXPECT_TRUE(run->Teardown().complete);
}

// T09-SGRPC-09
/** 验证调用期间关闭服务会取消 RPC，迟到完成不能修改新宿主。 */
TEST(T09_SGRPC_Fault, ShutdownDuringCallCancelsWithoutLateHostMutation) {
	auto store = std::make_shared<InMemoryStatusStore>();
	store->SetCount("chat-a", "0");
	store->ScheduleReadBlock();
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "29001"}}, store, std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	ASSERT_TRUE(server.Start("127.0.0.1:0"));
	auto stub = MakeStub(server.BoundEndpoint());
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 2s);
	message::GetChatServerRsp response;
	auto call = std::async(std::launch::async, /** 异步发起选服，供服务关闭与其并发发生。 */ [&] {
		return InvokeAssignment(*stub, context, 109, response);
	});
	const bool entered = store->WaitForReadBlocked(std::chrono::steady_clock::now() + 1s);
	auto stop = std::async(std::launch::async, /** 在独立任务中按短截止时间停止 gRPC 服务。 */ [&] {
		return server.Stop(std::chrono::system_clock::now() + 150ms);
	});
	ASSERT_TRUE(entered);
	const bool cancelled = call.wait_until(std::chrono::steady_clock::now() + 1s) == std::future_status::ready;
	store->ReleaseRead();
	ASSERT_TRUE(cancelled);
	ASSERT_EQ(stop.wait_until(std::chrono::steady_clock::now() + 2s), std::future_status::ready);
	EXPECT_TRUE(stop.get());
	EXPECT_NE(rpc::ClassifyStatus(call.get()), rpc::Failure::None);
	EXPECT_FALSE(server.Ready());
	EXPECT_TRUE(server.BoundEndpoint().empty());
}

// T09-SGRPC-10
/** 验证旧代 RPC 的迟到完成不能穿越服务重启边界。 */
TEST(T09_SGRPC_Fault, LateCompletionCannotCrossRestartGeneration) {
	auto store = std::make_shared<InMemoryStatusStore>();
	store->SetCount("chat-a", "0");
	store->ScheduleReadBlock();
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "29001"}}, store, std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	ASSERT_TRUE(server.Start("127.0.0.1:0"));
	auto old_stub = MakeStub(server.BoundEndpoint());
	grpc::ClientContext old_context;
	old_context.set_deadline(std::chrono::system_clock::now() + 100ms);
	message::GetChatServerRsp old_response;
	auto old_call = std::async(std::launch::async, /** 通过旧服务桩发起将超时的选服请求。 */ [&] {
		return InvokeAssignment(*old_stub, old_context, 110, old_response);
	});
	const bool entered = store->WaitForReadBlocked(std::chrono::steady_clock::now() + 1s);
	const bool expired = old_call.wait_until(std::chrono::steady_clock::now() + 1s) == std::future_status::ready;
	store->ReleaseRead();
	ASSERT_TRUE(entered);
	ASSERT_TRUE(expired);
	EXPECT_EQ(rpc::ClassifyStatus(old_call.get()), rpc::Failure::DeadlineExceeded);
	ASSERT_TRUE(store->WaitForReadCompleted(std::chrono::steady_clock::now() + 1s));
	EXPECT_TRUE(old_response.host().empty());
	EXPECT_TRUE(old_response.port().empty());
	EXPECT_TRUE(old_response.token().empty());
	ASSERT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));

	ASSERT_TRUE(server.Start("127.0.0.1:0"));
	auto new_stub = MakeStub(server.BoundEndpoint());
	grpc::ClientContext new_context;
	new_context.set_deadline(std::chrono::system_clock::now() + 1s);
	message::GetChatServerRsp new_response;
	const auto new_result = InvokeAssignment(*new_stub, new_context, 111, new_response);
	EXPECT_TRUE(new_result.ok());
	EXPECT_EQ(new_response.error(), kSuccess);
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
}

// T09-SGRPC-11
/** 验证占用端口启动失败且不夺取现有监听者。 */
TEST(T09_SGRPC_Fault, OccupiedPortStartupFailsWithoutStealingListener) {
	auto run = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = run->ReserveLoopbackPort("status-occupied");
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "29001"}},
		std::make_shared<InMemoryStatusStore>(), std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	EXPECT_FALSE(server.Start(endpoint.address + ":" + std::to_string(endpoint.port)));
	ASSERT_TRUE(run->ReleaseLoopbackPort("status-occupied"));
	ASSERT_TRUE(server.Start(endpoint.address + ":" + std::to_string(endpoint.port)));
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
	EXPECT_TRUE(run->Teardown().complete);
}

// T09-SGRPC-12
/** 验证停止释放服务资源与端口，允许同地址重启。 */
TEST(T09_SGRPC_Fault, StopReleasesServerResourcesAndPortForRestart) {
	auto run = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = run->ReserveLoopbackPort("status-restart");
	ASSERT_TRUE(run->ReleaseLoopbackPort("status-restart"));
	auto routing = status_routing_internal::CreateStatusRouting(
		{{"chat-a", "127.0.0.1", "29001"}},
		std::make_shared<InMemoryStatusStore>(), std::make_shared<FixedTokenSource>());
	status::StatusGrpcServer server(*routing);
	const auto address = endpoint.address + ":" + std::to_string(endpoint.port);
	ASSERT_TRUE(server.Start(address));
	ASSERT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
	ASSERT_TRUE(server.Start(address));
	EXPECT_TRUE(server.Stop(std::chrono::system_clock::now() + 2s));
	EXPECT_TRUE(run->Teardown().complete);
}

} // namespace
