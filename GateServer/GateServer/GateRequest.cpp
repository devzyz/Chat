#include "GateRequestInternal.h"

#include <utility>

#include "const.h"

namespace gate::internal {
namespace {

constexpr char kRegisterPurpose[] = "register";
constexpr char kResetPasswordPurpose[] = "reset_password";

// 请求协议按字节限制非空字符串，缺少字段时 JSON 值为 null。
bool IsValidText(const Json::Value& value, std::size_t max_bytes) {
    if (!value.isString())
        return false;
    const auto size = value.asString().size();
    return size > 0 && size <= max_bytes;
}

// 组织账号请求的处理顺序，共享持有下方四个业务依赖。
class GateRequestImpl final : public GateRequest {
public:
    // 四个依赖分别负责发送验证码、管理一次性验证码、读写账号和管理登录会话。
    GateRequestImpl(
        std::shared_ptr<VerificationPort> verification_sender,
        std::shared_ptr<CodeStore> verification_codes,
        std::shared_ptr<UserStore> accounts,
        std::shared_ptr<StatusPort> session_service)
        : verification_sender_(std::move(verification_sender)), verification_codes_(std::move(verification_codes)),
          accounts_(std::move(accounts)), session_service_(std::move(session_service)) {}

    // 校验公共字段后分派请求，依赖抛出的异常统一返回 RPCFailed。
    Result Handle(Endpoint endpoint, const Json::Value& request) override {
        try {
            if (!request.isObject())
                return {ErrorCodes::Error_Json};
            if (endpoint != Endpoint::Logout) {
                const auto& email = request["email"];
                if (!IsValidText(email, 254) || email.asString().find('@') == std::string::npos) {
                    return {ErrorCodes::Error_Json};
                }
            }

            switch (endpoint) {
            case Endpoint::GetVarifyCode:
                return RequestVerificationCode(request);
            case Endpoint::UserRegister:
                return RegisterUser(request);
            case Endpoint::ResetPassword:
                return ResetPassword(request);
            case Endpoint::UserLogin:
                return LogIn(request);
            case Endpoint::Logout:
                return LogOut(request);
            }
            return {ErrorCodes::RPCFailed};
        } catch (...) {
            return {ErrorCodes::RPCFailed};
        }
    }

private:
    // 缺省用途为注册，找回密码需显式指定用途。
    Result RequestVerificationCode(const Json::Value& request) {
        const auto purpose = request.get("purpose", kRegisterPurpose);
        if (!purpose.isString())
            return {ErrorCodes::Error_Json};
        const auto purpose_name = purpose.asString();
        if (purpose_name != kRegisterPurpose && purpose_name != kResetPasswordPurpose) {
            return {ErrorCodes::Error_Json};
        }
        return {verification_sender_->RequestCode(request["email"].asString(), purpose_name)};
    }

    // 仅读取并比对验证码，调用方在账号检查通过后再消费。
    ErrorCodes CheckVerificationCode(const std::string& email, const std::string& code, const char* purpose) {
        const auto stored_code = verification_codes_->ReadCode(email, purpose);
        if (!stored_code)
            return ErrorCodes::VarifyExpired;
        return *stored_code == code ? ErrorCodes::Success : ErrorCodes::VarifyCodeErr;
    }

    // 两次密码一致且注册验证码消费成功后才创建账号。
    Result RegisterUser(const Json::Value& request) {
        if (!IsValidText(request["user"], 1020) || !IsValidText(request["passwd"], 255) ||
            !IsValidText(request["confirm"], 255) || !IsValidText(request["varifycode"], 64)) {
            return {ErrorCodes::Error_Json};
        }
        const auto password = request["passwd"].asString();
        if (password != request["confirm"].asString())
            return {ErrorCodes::PasswdErr};

        const auto email = request["email"].asString();
        const auto code = request["varifycode"].asString();
        const auto error = CheckVerificationCode(email, code, kRegisterPurpose);
        if (error != ErrorCodes::Success)
            return {error};
        if (!verification_codes_->ConsumeCode(email, code, kRegisterPurpose))
            return {ErrorCodes::VarifyExpired};

        const int uid = accounts_->CreateUser(request["user"].asString(), email, password);
        return {uid == 0 || uid == -1 ? ErrorCodes::UserExist : ErrorCodes::Success};
    }

    // 先核对验证码和账号归属，再消费验证码并更新密码。
    Result ResetPassword(const Json::Value& request) {
        if (!IsValidText(request["user"], 1020) || !IsValidText(request["password"], 255) ||
            !IsValidText(request["varify"], 64)) {
            return {ErrorCodes::Error_Json};
        }
        const auto email = request["email"].asString();
        const auto username = request["user"].asString();
        const auto code = request["varify"].asString();
        const auto error = CheckVerificationCode(email, code, kResetPasswordPurpose);
        if (error != ErrorCodes::Success)
            return {error};
        if (!accounts_->IdentityMatches(username, email))
            return {ErrorCodes::EmailNotMatch};
        if (!verification_codes_->ConsumeCode(email, code, kResetPasswordPurpose))
            return {ErrorCodes::VarifyExpired};

        const bool updated = accounts_->UpdatePassword(username, request["password"].asString(), email);
        return {updated ? ErrorCodes::Success : ErrorCodes::PasswdUpFailed};
    }

    // 凭据通过后，为客户端取得 ChatServer 地址和登录令牌。
    Result LogIn(const Json::Value& request) {
        if (!IsValidText(request["password"], 255))
            return {ErrorCodes::Error_Json};
        const auto user = accounts_->CheckCredentials(request["email"].asString(), request["password"].asString());
        if (!user)
            return {ErrorCodes::PasswdInvalid};

        const auto assignment = session_service_->Assign(user->uid, user->auth_version);
        if (assignment.error != ErrorCodes::Success)
            return {ErrorCodes::RPCFailed};
        return {ErrorCodes::Success, user->uid, assignment.token, assignment.host, assignment.port};
    }

    // 仅撤销与该用户当前会话匹配的令牌。
    Result LogOut(const Json::Value& request) {
        const auto& uid = request["uid"];
        if (!uid.isInt() || uid.asInt() <= 0 || !IsValidText(request["token"], 128))
            return {ErrorCodes::Error_Json};
        const bool revoked = session_service_->Revoke(uid.asInt(), request["token"].asString());
        return {revoked ? ErrorCodes::Success : ErrorCodes::TokenInvalid};
    }

    std::shared_ptr<VerificationPort> verification_sender_;
    std::shared_ptr<CodeStore> verification_codes_;
    std::shared_ptr<UserStore> accounts_;
    std::shared_ptr<StatusPort> session_service_;
};

} // namespace

std::unique_ptr<GateRequest> CreateGateRequest(
    std::shared_ptr<VerificationPort> verification_sender,
    std::shared_ptr<CodeStore> verification_codes,
    std::shared_ptr<UserStore> accounts,
    std::shared_ptr<StatusPort> session_service) {
    return std::make_unique<GateRequestImpl>(
        std::move(verification_sender),
        std::move(verification_codes),
        std::move(accounts),
        std::move(session_service));
}

} // namespace gate::internal
