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

class InMemoryStatusStore final : public status_routing_internal::StatusStore {
public:
	std::optional<std::string> ReadCount(const std::string& name) override {
		{
			std::unique_lock<std::mutex> lock(fault_mutex_);
			if (block_next_read_) {
				block_next_read_ = false;
				read_blocked_ = true;
				fault_condition_.notify_all();
				fault_condition_.wait(lock, [this] { return release_read_; });
				read_completed_ = true;
				fault_condition_.notify_all();
			}
		}
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = counts_.find(name);
		return found == counts_.end() ? std::nullopt : std::optional<std::string>(found->second);
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

	void SetCount(std::string name, std::string count) {
		std::lock_guard<std::mutex> lock(mutex_);
		counts_[std::move(name)] = std::move(count);
	}

	void ScheduleReadBlock() {
		std::lock_guard<std::mutex> lock(fault_mutex_);
		block_next_read_ = true;
		read_blocked_ = false;
		release_read_ = false;
		read_completed_ = false;
	}

	bool WaitForReadBlocked(std::chrono::steady_clock::time_point deadline) {
		std::unique_lock<std::mutex> lock(fault_mutex_);
		return fault_condition_.wait_until(lock, deadline, [this] { return read_blocked_; });
	}

	void ReleaseRead() {
		{
			std::lock_guard<std::mutex> lock(fault_mutex_);
			release_read_ = true;
		}
		fault_condition_.notify_all();
	}

	bool WaitForReadCompleted(std::chrono::steady_clock::time_point deadline) {
		std::unique_lock<std::mutex> lock(fault_mutex_);
		return fault_condition_.wait_until(lock, deadline, [this] { return read_completed_; });
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

class FixedTokenSource final : public status_routing_internal::TokenSource {
public:
	std::string Next() override {
		return "SYNTHETIC_STATUS_GRPC_TOKEN";
	}
};

class T09_SGRPC_Core : public testing::Test {
protected:
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

	void TearDown() override {
		if (server_) {
			EXPECT_TRUE(server_->Stop(std::chrono::system_clock::now() + 2s));
		}
	}

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
TEST_F(T09_SGRPC_Core, PublishesProtocolReadyNumericLoopbackEndpoint) {
	EXPECT_TRUE(channel_->WaitForConnected(std::chrono::system_clock::now() + 2s));
	EXPECT_EQ(server_->BoundAddress(), "127.0.0.1");
	EXPECT_NE(server_->BoundPort(), 0);
}

// T09-SGRPC-02 and T09-SGRPC-04 (maximum valid int32 request field)
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

std::unique_ptr<message::StatusService::Stub> MakeStub(const std::string& endpoint) {
	return message::StatusService::NewStub(
		grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
}

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
	auto call = std::async(std::launch::async, [&] {
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
	auto call = std::async(std::launch::async, [&] {
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
TEST(T09_SGRPC_Fault, RefusedConnectionIsBoundedAndSanitized) {
	auto run = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = run->ReserveLoopbackPort("status-refused");
	ASSERT_TRUE(run->ReleaseLoopbackPort("status-refused"));
	auto stub = MakeStub(endpoint.address + ":" + std::to_string(endpoint.port));
	grpc::ClientContext context;
	context.set_deadline(std::chrono::system_clock::now() + 500ms);
	message::GetChatServerRsp response;
	const auto result = InvokeAssignment(*stub, context, 108, response);
	EXPECT_EQ(rpc::ClassifyStatus(result), rpc::Failure::Unavailable);
	EXPECT_TRUE(result.error_message().find("SYNTHETIC") == std::string::npos);
	EXPECT_TRUE(run->Teardown().complete);
}

// T09-SGRPC-09
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
	auto call = std::async(std::launch::async, [&] {
		return InvokeAssignment(*stub, context, 109, response);
	});
	const bool entered = store->WaitForReadBlocked(std::chrono::steady_clock::now() + 1s);
	auto stop = std::async(std::launch::async, [&] {
		return server.Stop(std::chrono::system_clock::now() + 1s);
	});
	store->ReleaseRead();
	ASSERT_TRUE(entered);
	ASSERT_EQ(stop.wait_until(std::chrono::steady_clock::now() + 2s), std::future_status::ready);
	EXPECT_TRUE(stop.get());
	ASSERT_EQ(call.wait_until(std::chrono::steady_clock::now() + 2s), std::future_status::ready);
	EXPECT_NE(rpc::ClassifyStatus(call.get()), rpc::Failure::None);
	EXPECT_FALSE(server.Ready());
	EXPECT_TRUE(server.BoundEndpoint().empty());
}

// T09-SGRPC-10
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
	auto old_call = std::async(std::launch::async, [&] {
		return InvokeAssignment(*old_stub, old_context, 110, old_response);
	});
	const bool entered = store->WaitForReadBlocked(std::chrono::steady_clock::now() + 1s);
	const bool expired = old_call.wait_until(std::chrono::steady_clock::now() + 1s) == std::future_status::ready;
	store->ReleaseRead();
	ASSERT_TRUE(entered);
	ASSERT_TRUE(expired);
	EXPECT_EQ(rpc::ClassifyStatus(old_call.get()), rpc::Failure::DeadlineExceeded);
	ASSERT_TRUE(store->WaitForReadCompleted(std::chrono::steady_clock::now() + 1s));
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
