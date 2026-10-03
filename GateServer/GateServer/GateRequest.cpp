#include "GateRequestInternal.h"

#include "const.h"

#include <utility>

namespace gate::internal {
namespace {

/** @brief 共享持有四个业务端口，按端点顺序调用并统一映射异常。 */
class GateRequestImpl final : public GateRequest {
public:
    /** @brief 保存四个非空依赖，供后续同步请求复用。 */
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

    /** @brief 执行业务校验及依赖调用；首个失败立即返回，异常统一映射为 RPCFailed。 */
	Result Handle(Endpoint endpoint, const Json::Value& request) override {
        try {
            if (!request.isObject()) return {ErrorCodes::Error_Json};
            const auto text = /** @brief 校验字符串类型、非空和编码字节上限，不读取无关字段。 */
                [&request](const char* key, std::size_t limit) {
                    return request[key].isString() && !request[key].asString().empty()
                        && request[key].asString().size() <= limit;
                };
            if (endpoint == Endpoint::Logout) {
                if (!request["uid"].isInt() || request["uid"].asInt() <= 0 || !text("token", 128))
                    return {ErrorCodes::Error_Json};
                return {status_->Revoke(request["uid"].asInt(), request["token"].asString())
                    ? ErrorCodes::Success : ErrorCodes::TokenInvalid};
            }
            if (!text("email", 254) || request["email"].asString().find('@') == std::string::npos)
                return {ErrorCodes::Error_Json};
            if (endpoint == Endpoint::UserRegister && (!text("user", 1020) || !text("passwd", 255)
                || !text("confirm", 255) || !text("varifycode", 64))) return {ErrorCodes::Error_Json};
            if (endpoint == Endpoint::ResetPassword && (!text("user", 1020) || !text("password", 255)
                || !text("varify", 64))) return {ErrorCodes::Error_Json};
            if (endpoint == Endpoint::UserLogin && !text("password", 255)) return {ErrorCodes::Error_Json};
            std::string purpose = endpoint == Endpoint::ResetPassword ? "reset_password" : "register";
            if (endpoint == Endpoint::GetVarifyCode && request.isMember("purpose")) {
                if (!request["purpose"].isString()) return {ErrorCodes::Error_Json};
                purpose = request["purpose"].asString();
                if (purpose != "register" && purpose != "reset_password") return {ErrorCodes::Error_Json};
            }
            if (endpoint == Endpoint::GetVarifyCode) {
				if (!request.isMember("email")) {
					return {ErrorCodes::Error_Json};
				}
				return {verification_->RequestCode(request["email"].asString(), purpose)};
			}
			if (endpoint == Endpoint::UserRegister) {
				if (request["passwd"].asString() != request["confirm"].asString()) {
					return {ErrorCodes::PasswdErr};
				}
				const auto code = code_store_->ReadCode(request["email"].asString(), purpose);
				if (!code.has_value()) {
					return {ErrorCodes::VarifyExpired};
				}
				if (*code != request["varifycode"].asString()) {
					return {ErrorCodes::VarifyCodeErr};
				}
                if (!code_store_->ConsumeCode(request["email"].asString(), *code, purpose)) return {ErrorCodes::VarifyExpired};
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
				const auto code = code_store_->ReadCode(request["email"].asString(), purpose);
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
                if (!code_store_->ConsumeCode(request["email"].asString(), *code, purpose)) return {ErrorCodes::VarifyExpired};
				if (!user_store_->UpdatePassword(
						request["user"].asString(), request["password"].asString(), request["email"].asString())) {
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
				const auto assignment = status_->Assign(user->uid, user->auth_version);
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
