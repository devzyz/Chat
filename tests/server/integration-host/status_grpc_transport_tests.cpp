#include <gtest/gtest.h>

#include "../../../StatusServer/StatusServer/StatusGrpcServer.h"
#include "../../../StatusServer/StatusServer/StatusRoutingInternal.h"
#include "../../../generated/proto/cpp/status.grpc.pb.h"

#include <chrono>
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

private:
	std::mutex mutex_;
	std::unordered_map<std::string, std::string> counts_;
	std::unordered_map<int, std::string> tokens_;
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

} // namespace
