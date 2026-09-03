#include "GateRequestInternal.h"

#include "const.h"

#include <utility>

namespace gate::internal {
namespace {

class GateRequestImpl final : public GateRequest {
public:
	GateRequestImpl(
		std::shared_ptr<VerificationPort> verification,
		std::shared_ptr<CodeStore> code_store,
		std::shared_ptr<UserStore> user_store,
		std::shared_ptr<StatusPort> status)
		: verification_(std::move(verification)),
		  code_store_(std::move(code_store)),
		  user_store_(std::move(user_store)),
		  status_(std::move(status)) {
	}

	Result Handle(Endpoint endpoint, const Json::Value& request) override {
		try {
			if (endpoint == Endpoint::GetVarifyCode) {
				if (!request.isMember("email")) {
					return {ErrorCodes::Error_Json};
				}
				return {verification_->RequestCode(request["email"].asString())};
			}
			if (endpoint == Endpoint::UserRegister) {
				if (request["passwd"].asString() != request["confirm"].asString()) {
					return {ErrorCodes::PasswdErr};
				}
				const auto code = code_store_->ReadCode(request["email"].asString());
				if (!code.has_value()) {
					return {ErrorCodes::VarifyExpired};
				}
				if (*code != request["varifycode"].asString()) {
					return {ErrorCodes::VarifyCodeErr};
				}
				const auto uid = user_store_->CreateUser(
					request["user"].asString(),
					request["email"].asString(),
					request["passwd"].asString());
				if (uid == 0 || uid == -1) {
					return {ErrorCodes::UserExist};
				}
				return {ErrorCodes::Success};
			}
			if (endpoint == Endpoint::ResetPassword) {
				const auto code = code_store_->ReadCode(request["email"].asString());
				if (!code.has_value()) {
					return {ErrorCodes::VarifyExpired};
				}
				if (*code != request["varify"].asString()) {
					return {ErrorCodes::VarifyCodeErr};
				}
				if (!user_store_->IdentityMatches(
						request["user"].asString(), request["email"].asString())) {
					return {ErrorCodes::EmailNotMatch};
				}
				if (!user_store_->UpdatePassword(
						request["user"].asString(), request["password"].asString())) {
					return {ErrorCodes::PasswdUpFailed};
				}
				return {ErrorCodes::Success};
			}
			if (endpoint == Endpoint::UserLogin) {
				const auto user = user_store_->CheckCredentials(
					request["email"].asString(), request["password"].asString());
				if (!user.has_value()) {
					return {ErrorCodes::PasswdInvalid};
				}
				const auto assignment = status_->Assign(user->uid);
				if (assignment.error != ErrorCodes::Success) {
					return {ErrorCodes::RPCFailed};
				}
				return {
					ErrorCodes::Success,
					user->uid,
					assignment.token,
					assignment.host,
					assignment.port,
				};
			}
		}
		catch (...) {
			return {ErrorCodes::RPCFailed};
		}
		return {ErrorCodes::RPCFailed};
	}

private:
	std::shared_ptr<VerificationPort> verification_;
	std::shared_ptr<CodeStore> code_store_;
	std::shared_ptr<UserStore> user_store_;
	std::shared_ptr<StatusPort> status_;
};

} // namespace

std::unique_ptr<GateRequest> CreateGateRequest(
	std::shared_ptr<VerificationPort> verification,
	std::shared_ptr<CodeStore> code_store,
	std::shared_ptr<UserStore> user_store,
	std::shared_ptr<StatusPort> status) {
	return std::make_unique<GateRequestImpl>(
		std::move(verification),
		std::move(code_store),
		std::move(user_store),
		std::move(status));
}

} // namespace gate::internal
