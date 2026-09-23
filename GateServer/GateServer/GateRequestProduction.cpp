#include "GateRequestProduction.h"

#include "GateRequestInternal.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "VerifyGrpcClient.h"
#include "const.h"

#include <optional>

namespace {

/** @brief 将验证码请求同步转发给现有 gRPC 客户端。 */
class GrpcVerificationAdapter final : public gate::internal::VerificationPort {
public:
    /** @brief 返回 gRPC 回复中的业务错误码，保留对外 GetVarifyCode 协议名称。 */
	int RequestCode(const std::string& email) override {
		return VerifyGrpcClient::GetInstance()->GetVarifyCode(email).error();
	}
};

/** @brief 从现有 Redis 验证码键读取值，不消费验证码。 */
class RedisCodeStoreAdapter final : public gate::internal::CodeStore {
public:
    /** @brief 返回验证码副本；Redis Get 返回 false 时统一返回 nullopt。 */
	std::optional<std::string> ReadCode(const std::string& email) override {
		std::string code;
		if (!RedisMgr::GetInstance()->Get(CODEPREFIX + email, code)) {
			return std::nullopt;
		}
		return code;
	}
};

/** @brief 将账号端口适配到共享 MysqlMgr，不缓存请求或凭据。 */
class MysqlUserStoreAdapter final : public gate::internal::UserStore {
public:
    /** @brief 转交账号创建并保留管理器的 UID 或失败哨兵返回值。 */
	int CreateUser(
		const std::string& username,
		const std::string& email,
		const std::string& password) override {
		return MysqlMgr::GetInstance()->RegUser(username, email, password);
	}

    /** @brief 查询用户名与邮箱是否匹配，保留管理器的失败结果。 */
	bool IdentityMatches(
		const std::string& username,
		const std::string& email) override {
		return MysqlMgr::GetInstance()->CheckEmail(username, email);
	}

    /** @brief 同步更新指定用户密码，直接返回管理器结果。 */
	bool UpdatePassword(
		const std::string& username,
		const std::string& password) override {
		return MysqlMgr::GetInstance()->UpdatePassword(username, password);
	}

    /** @brief 校验邮箱及密码，仅返回认证 UID；校验失败返回 nullopt。 */
	std::optional<gate::internal::UserRecord> CheckCredentials(
		const std::string& email,
		const std::string& password) override {
		UserInfo user;
		if (!MysqlMgr::GetInstance()->CheckPassword(email, password, user)) {
			return std::nullopt;
		}
		return gate::internal::UserRecord{user.uid};
	}
};

/** @brief 将选服端口适配到共享 Status gRPC 客户端。 */
class GrpcStatusAdapter final : public gate::internal::StatusPort {
public:
    /** @brief 同步获取选服响应并复制业务码、Token 和地址，不额外推断成功。 */
	gate::internal::StatusAssignment Assign(int uid) override {
		const auto response = StatusGrpcClient::GetInstance()->GetChatServer(uid);
		return {
			response.error(),
			response.token(),
			response.host(),
			response.port(),
		};
	}
};

} // namespace

namespace gate {

std::unique_ptr<GateRequest> CreateProductionGateRequest() {
	return internal::CreateGateRequest(
		std::make_shared<GrpcVerificationAdapter>(),
		std::make_shared<RedisCodeStoreAdapter>(),
		std::make_shared<MysqlUserStoreAdapter>(),
		std::make_shared<GrpcStatusAdapter>());
}

} // namespace gate
