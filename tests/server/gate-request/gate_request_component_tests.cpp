#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GateRequestInternal.h"

namespace {

constexpr int kSuccess = 0;
constexpr int kJsonError = 1001;
constexpr int kRpcFailed = 1002;
constexpr int kExpired = 1003;
constexpr int kCodeMismatch = 1004;
constexpr int kUserExists = 1005;
constexpr int kPasswordMismatch = 1006;
constexpr int kIdentityMismatch = 1007;
constexpr int kPasswordUpdateFailed = 1008;
constexpr int kCredentialsInvalid = 1009;

constexpr const char* kEmail = "gate-3a04@example.test";
constexpr const char* kUsername = "gate-3a04-user";
constexpr const char* kPassword = "gate-3a04-password";
constexpr const char* kCode = "3A04";

class VerificationAdapter final : public gate::internal::VerificationPort {
public:
	explicit VerificationAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	int RequestCode(const std::string&) override {
		calls_->push_back("verification");
		if (throw_on_call) {
			throw std::runtime_error("synthetic verification failure");
		}
		return result;
	}

	int result = kSuccess;
	bool throw_on_call = false;

private:
	std::shared_ptr<std::vector<std::string>> calls_;
};

class CodeStoreAdapter final : public gate::internal::CodeStore {
public:
	explicit CodeStoreAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	std::optional<std::string> ReadCode(const std::string&) override {
		calls_->push_back("code.read");
		if (throw_on_call) {
			throw std::runtime_error("synthetic code-store failure");
		}
		return code;
	}

	std::optional<std::string> code;
	bool throw_on_call = false;

private:
	std::shared_ptr<std::vector<std::string>> calls_;
};

class UserStoreAdapter final : public gate::internal::UserStore {
public:
	explicit UserStoreAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	int CreateUser(const std::string&, const std::string&, const std::string&) override {
		calls_->push_back("user.create");
		MaybeThrow();
		return create_result;
	}

	bool IdentityMatches(const std::string&, const std::string&) override {
		calls_->push_back("user.identity");
		MaybeThrow();
		return identity_matches;
	}

	bool UpdatePassword(const std::string&, const std::string&) override {
		calls_->push_back("user.update");
		MaybeThrow();
		return update_result;
	}

	std::optional<gate::internal::UserRecord> CheckCredentials(
		const std::string&, const std::string&) override {
		calls_->push_back("user.credentials");
		MaybeThrow();
		return credentials;
	}

	int create_result = 0;
	bool identity_matches = false;
	bool update_result = false;
	std::optional<gate::internal::UserRecord> credentials;
	bool throw_on_call = false;

private:
	void MaybeThrow() const {
		if (throw_on_call) {
			throw std::runtime_error("synthetic user-store failure");
		}
	}

	std::shared_ptr<std::vector<std::string>> calls_;
};

class StatusAdapter final : public gate::internal::StatusPort {
public:
	explicit StatusAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	gate::internal::StatusAssignment Assign(int) override {
		calls_->push_back("status.assign");
		if (throw_on_call) {
			throw std::runtime_error("synthetic status failure");
		}
		return assignment;
	}

	gate::internal::StatusAssignment assignment;
	bool throw_on_call = false;

private:
	std::shared_ptr<std::vector<std::string>> calls_;
};

class GateRequestComponentTests : public testing::Test {
protected:
	GateRequestComponentTests()
		: calls(std::make_shared<std::vector<std::string>>()),
		  verification(std::make_shared<VerificationAdapter>(calls)),
		  code_store(std::make_shared<CodeStoreAdapter>(calls)),
		  user_store(std::make_shared<UserStoreAdapter>(calls)),
		  status(std::make_shared<StatusAdapter>(calls)),
		  module(gate::internal::CreateGateRequest(
			  verification, code_store, user_store, status)) {
	}

	Json::Value RegistrationRequest() const {
		Json::Value request;
		request["email"] = kEmail;
		request["user"] = kUsername;
		request["passwd"] = kPassword;
		request["confirm"] = kPassword;
		request["varifycode"] = kCode;
		return request;
	}

	Json::Value ResetRequest() const {
		Json::Value request;
		request["email"] = kEmail;
		request["user"] = kUsername;
		request["password"] = kPassword;
		request["varify"] = kCode;
		return request;
	}

	Json::Value LoginRequest() const {
		Json::Value request;
		request["email"] = kEmail;
		request["password"] = kPassword;
		return request;
	}

	void ExpectResult(const gate::Result& result, int error) const {
		EXPECT_EQ(result.error, error);
	}

	std::shared_ptr<std::vector<std::string>> calls;
	std::shared_ptr<VerificationAdapter> verification;
	std::shared_ptr<CodeStoreAdapter> code_store;
	std::shared_ptr<UserStoreAdapter> user_store;
	std::shared_ptr<StatusAdapter> status;
	std::unique_ptr<gate::GateRequest> module;
};

TEST_F(GateRequestComponentTests, VerificationWithoutEmailFailsBeforeAdapters) {
	// T08-GATE-01
	const auto result = module->Handle(gate::Endpoint::GetVarifyCode, Json::Value{});
	ExpectResult(result, kJsonError);
	EXPECT_TRUE(calls->empty());
}

TEST_F(GateRequestComponentTests, VerificationSuccessReturnsAfterOneRpcCall) {
	// T08-GATE-02
	Json::Value request;
	request["email"] = kEmail;
	verification->result = kSuccess;

	const auto result = module->Handle(gate::Endpoint::GetVarifyCode, request);

	ExpectResult(result, kSuccess);
	EXPECT_EQ(*calls, std::vector<std::string>{"verification"});
}

TEST_F(GateRequestComponentTests, VerificationRpcFailureFailsClosedAfterOneCall) {
	// T08-GATE-03
	Json::Value request;
	request["email"] = kEmail;
	verification->result = kRpcFailed;

	const auto result = module->Handle(gate::Endpoint::GetVarifyCode, request);

	ExpectResult(result, kRpcFailed);
	EXPECT_EQ(*calls, std::vector<std::string>{"verification"});

	calls->clear();
	verification->throw_on_call = true;
	const auto exception_result = module->Handle(gate::Endpoint::GetVarifyCode, request);
	ExpectResult(exception_result, kRpcFailed);
	EXPECT_EQ(*calls, std::vector<std::string>{"verification"});
}

TEST_F(GateRequestComponentTests, RegistrationPasswordMismatchStopsBeforeCodeRead) {
	// T08-GATE-04
	auto request = RegistrationRequest();
	request["confirm"] = "different-confirmation";

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kPasswordMismatch);
	EXPECT_TRUE(calls->empty());
}

TEST_F(GateRequestComponentTests, RegistrationExpiredCodeStopsBeforeUserCreate) {
	// T08-GATE-05
	auto request = RegistrationRequest();
	code_store->code = std::nullopt;

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kExpired);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});

	calls->clear();
	code_store->throw_on_call = true;
	const auto exception_result = module->Handle(gate::Endpoint::UserRegister, request);
	ExpectResult(exception_result, kRpcFailed);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

TEST_F(GateRequestComponentTests, RegistrationCodeMismatchStopsBeforeUserCreate) {
	// T08-GATE-06
	auto request = RegistrationRequest();
	code_store->code = "different-code";

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kCodeMismatch);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

TEST_F(GateRequestComponentTests, RegistrationExistingUserFailsAfterOrderedChecks) {
	// T08-GATE-07
	auto request = RegistrationRequest();
	code_store->code = kCode;
	user_store->create_result = 0;

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kUserExists);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.create"}));

	calls->clear();
	user_store->throw_on_call = true;
	const auto exception_result = module->Handle(gate::Endpoint::UserRegister, request);
	ExpectResult(exception_result, kRpcFailed);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.create"}));
}

TEST_F(GateRequestComponentTests, RegistrationSuccessUsesCodeBeforeUserCreate) {
	// T08-GATE-08
	auto request = RegistrationRequest();
	code_store->code = kCode;
	user_store->create_result = 42;

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kSuccess);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.create"}));
}

TEST_F(GateRequestComponentTests, ResetExpiredCodeStopsBeforeIdentityCheck) {
	// T08-GATE-09
	auto request = ResetRequest();
	code_store->code = std::nullopt;

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kExpired);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

TEST_F(GateRequestComponentTests, ResetCodeMismatchStopsBeforeIdentityCheck) {
	// T08-GATE-10
	auto request = ResetRequest();
	code_store->code = "different-code";

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kCodeMismatch);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

TEST_F(GateRequestComponentTests, ResetIdentityMismatchStopsBeforePasswordUpdate) {
	// T08-GATE-11
	auto request = ResetRequest();
	code_store->code = kCode;
	user_store->identity_matches = false;

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kIdentityMismatch);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.identity"}));
}

TEST_F(GateRequestComponentTests, ResetUpdateFailureReturnsStableBusinessError) {
	// T08-GATE-12
	auto request = ResetRequest();
	code_store->code = kCode;
	user_store->identity_matches = true;
	user_store->update_result = false;

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kPasswordUpdateFailed);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.identity", "user.update"}));
}

TEST_F(GateRequestComponentTests, ResetSuccessUsesCodeIdentityThenPasswordUpdate) {
	// T08-GATE-13
	auto request = ResetRequest();
	code_store->code = kCode;
	user_store->identity_matches = true;
	user_store->update_result = true;

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kSuccess);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.identity", "user.update"}));
}

TEST_F(GateRequestComponentTests, LoginCredentialFailureStopsBeforeStatusAssignment) {
	// T08-GATE-14
	auto request = LoginRequest();
	user_store->credentials = std::nullopt;

	const auto result = module->Handle(gate::Endpoint::UserLogin, request);

	ExpectResult(result, kCredentialsInvalid);
	EXPECT_EQ(*calls, std::vector<std::string>{"user.credentials"});
}

TEST_F(GateRequestComponentTests, LoginStatusFailureFailsClosedAfterCredentials) {
	// T08-GATE-15
	auto request = LoginRequest();
	user_store->credentials = gate::internal::UserRecord{42};
	status->assignment = gate::internal::StatusAssignment{kRpcFailed, {}, {}, {}};

	const auto result = module->Handle(gate::Endpoint::UserLogin, request);

	ExpectResult(result, kRpcFailed);
	EXPECT_EQ(*calls, (std::vector<std::string>{"user.credentials", "status.assign"}));

	calls->clear();
	status->throw_on_call = true;
	const auto exception_result = module->Handle(gate::Endpoint::UserLogin, request);
	ExpectResult(exception_result, kRpcFailed);
	EXPECT_EQ(*calls, (std::vector<std::string>{"user.credentials", "status.assign"}));
}

TEST_F(GateRequestComponentTests, LoginSuccessReturnsTheAssignedServerAfterCredentials) {
	// T08-GATE-16
	auto request = LoginRequest();
	user_store->credentials = gate::internal::UserRecord{42};
	status->assignment = gate::internal::StatusAssignment{
		kSuccess, "gate-3a04-token", "127.0.0.1", "8090"};

	const auto result = module->Handle(gate::Endpoint::UserLogin, request);

	ExpectResult(result, kSuccess);
	EXPECT_EQ(result.uid, 42);
	EXPECT_EQ(result.token, "gate-3a04-token");
	EXPECT_EQ(result.host, "127.0.0.1");
	EXPECT_EQ(result.port, "8090");
	EXPECT_EQ(*calls, (std::vector<std::string>{"user.credentials", "status.assign"}));
}

} // namespace
