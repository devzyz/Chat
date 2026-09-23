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

/** 记录验证码 RPC 调用并允许注入业务失败或异常。 */
class VerificationAdapter final : public gate::internal::VerificationPort {
public:
	/** 共享调用顺序记录器。 */
	explicit VerificationAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	/** 记录调用后返回指定验证码结果或抛测试异常。 */
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

/** 模拟验证码读取缺失、匹配或异常并记录访问顺序。 */
class CodeStoreAdapter final : public gate::internal::CodeStore {
public:
	/** 共享调用顺序记录器。 */
	explicit CodeStoreAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	/** 记录验证码读取后返回预设值或抛异常。 */
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

/** 模拟用户创建、身份核对、密码更新及凭据校验，记录依赖调用顺序。 */
class UserStoreAdapter final : public gate::internal::UserStore {
public:
	/** 共享调用顺序记录器。 */
	explicit UserStoreAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	/** 记录创建用户调用并返回预设用户编号或失败。 */
	int CreateUser(const std::string&, const std::string&, const std::string&) override {
		calls_->push_back("user.create");
		MaybeThrow();
		return create_result;
	}

	/** 记录身份核对并返回预设匹配结果。 */
	bool IdentityMatches(const std::string&, const std::string&) override {
		calls_->push_back("user.identity");
		MaybeThrow();
		return identity_matches;
	}

	/** 记录密码更新并返回预设写入结果。 */
	bool UpdatePassword(const std::string&, const std::string&) override {
		calls_->push_back("user.update");
		MaybeThrow();
		return update_result;
	}

	/** 记录凭据校验并返回预设用户身份。 */
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
	/** 故障开关开启时抛出测试存储异常。 */
	void MaybeThrow() const {
		if (throw_on_call) {
			throw std::runtime_error("synthetic user-store failure");
		}
	}

	std::shared_ptr<std::vector<std::string>> calls_;
};

/** 模拟选服端口并记录调用顺序，可注入业务失败和异常。 */
class StatusAdapter final : public gate::internal::StatusPort {
public:
	/** 共享调用顺序记录器。 */
	explicit StatusAdapter(std::shared_ptr<std::vector<std::string>> calls)
		: calls_(std::move(calls)) {
	}

	/** 记录选服调用后返回预设结果或抛异常。 */
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

/** 组合真实 Gate 编排模块与四个可控端口，验证短路和依赖顺序。 */
class GateRequestComponentTests : public testing::Test {
protected:
	/** 建立共享调用记录、端口替身及被测请求编排器。 */
	GateRequestComponentTests()
		: calls(std::make_shared<std::vector<std::string>>()),
		  verification(std::make_shared<VerificationAdapter>(calls)),
		  code_store(std::make_shared<CodeStoreAdapter>(calls)),
		  user_store(std::make_shared<UserStoreAdapter>(calls)),
		  status(std::make_shared<StatusAdapter>(calls)),
		  module(gate::internal::CreateGateRequest(
			  verification, code_store, user_store, status)) {
	}

	/** 构建字段完整且密码确认一致的注册请求。 */
	Json::Value RegistrationRequest() const {
		Json::Value request;
		request["email"] = kEmail;
		request["user"] = kUsername;
		request["passwd"] = kPassword;
		request["confirm"] = kPassword;
		request["varifycode"] = kCode;
		return request;
	}

	/** 构建字段完整的密码重置请求。 */
	Json::Value ResetRequest() const {
		Json::Value request;
		request["email"] = kEmail;
		request["user"] = kUsername;
		request["password"] = kPassword;
		request["varify"] = kCode;
		return request;
	}

	/** 构建邮箱与密码登录请求。 */
	Json::Value LoginRequest() const {
		Json::Value request;
		request["email"] = kEmail;
		request["password"] = kPassword;
		return request;
	}

	/** 断言编排返回的业务错误码。 */
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

/** 验证缺少邮箱在调用任何依赖前被拒绝。 */
TEST_F(GateRequestComponentTests, VerificationWithoutEmailFailsBeforeAdapters) {
	// T08-GATE-01
	const auto result = module->Handle(gate::Endpoint::GetVarifyCode, Json::Value{});
	ExpectResult(result, kJsonError);
	EXPECT_TRUE(calls->empty());
}

/** 验证验证码请求成功仅调用一次 RPC。 */
TEST_F(GateRequestComponentTests, VerificationSuccessReturnsAfterOneRpcCall) {
	// T08-GATE-02
	Json::Value request;
	request["email"] = kEmail;
	verification->result = kSuccess;

	const auto result = module->Handle(gate::Endpoint::GetVarifyCode, request);

	ExpectResult(result, kSuccess);
	EXPECT_EQ(*calls, std::vector<std::string>{"verification"});
}

/** 验证验证码 RPC 失败和异常均只调用一次并失败关闭。 */
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

/** 验证注册密码确认不一致时不读取验证码。 */
TEST_F(GateRequestComponentTests, RegistrationPasswordMismatchStopsBeforeCodeRead) {
	// T08-GATE-04
	auto request = RegistrationRequest();
	request["confirm"] = "different-confirmation";

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kPasswordMismatch);
	EXPECT_TRUE(calls->empty());
}

/** 验证验证码过期或读取异常时不创建用户。 */
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

/** 验证验证码不匹配时不创建用户。 */
TEST_F(GateRequestComponentTests, RegistrationCodeMismatchStopsBeforeUserCreate) {
	// T08-GATE-06
	auto request = RegistrationRequest();
	code_store->code = "different-code";

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kCodeMismatch);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

/** 验证用户已存在或创建异常时保留既定检查顺序。 */
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

/** 验证成功注册先读验证码再创建用户。 */
TEST_F(GateRequestComponentTests, RegistrationSuccessUsesCodeBeforeUserCreate) {
	// T08-GATE-08
	auto request = RegistrationRequest();
	code_store->code = kCode;
	user_store->create_result = 42;

	const auto result = module->Handle(gate::Endpoint::UserRegister, request);

	ExpectResult(result, kSuccess);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.create"}));
}

/** 验证重置验证码过期时不核对用户身份。 */
TEST_F(GateRequestComponentTests, ResetExpiredCodeStopsBeforeIdentityCheck) {
	// T08-GATE-09
	auto request = ResetRequest();
	code_store->code = std::nullopt;

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kExpired);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

/** 验证重置验证码不匹配时不核对用户身份。 */
TEST_F(GateRequestComponentTests, ResetCodeMismatchStopsBeforeIdentityCheck) {
	// T08-GATE-10
	auto request = ResetRequest();
	code_store->code = "different-code";

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kCodeMismatch);
	EXPECT_EQ(*calls, std::vector<std::string>{"code.read"});
}

/** 验证重置身份不匹配时不更新密码。 */
TEST_F(GateRequestComponentTests, ResetIdentityMismatchStopsBeforePasswordUpdate) {
	// T08-GATE-11
	auto request = ResetRequest();
	code_store->code = kCode;
	user_store->identity_matches = false;

	const auto result = module->Handle(gate::Endpoint::ResetPassword, request);

	ExpectResult(result, kIdentityMismatch);
	EXPECT_EQ(*calls, (std::vector<std::string>{"code.read", "user.identity"}));
}

/** 验证密码更新失败返回稳定业务错误。 */
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

/** 验证重置成功遵循验证码、身份、更新的顺序。 */
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

/** 验证登录凭据失败时不调用选服。 */
TEST_F(GateRequestComponentTests, LoginCredentialFailureStopsBeforeStatusAssignment) {
	// T08-GATE-14
	auto request = LoginRequest();
	user_store->credentials = std::nullopt;

	const auto result = module->Handle(gate::Endpoint::UserLogin, request);

	ExpectResult(result, kCredentialsInvalid);
	EXPECT_EQ(*calls, std::vector<std::string>{"user.credentials"});
}

/** 验证登录选服失败或异常仅在凭据通过后返回失败。 */
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

/** 验证登录成功返回凭据身份及选服端点。 */
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
