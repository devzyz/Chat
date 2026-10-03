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
	int RequestCode(const std::string& email, const std::string& purpose) override {
		return VerifyGrpcClient::GetInstance()->GetVarifyCode(email, purpose).error();
	}
};

/** @brief 对验证码校验限速，并以原子比较删除防止重复使用。 */
class RedisCodeStoreAdapter final : public gate::internal::CodeStore {
public:
    /** @brief 返回验证码副本；Redis Get 返回 false 时统一返回 nullopt。 */
	std::optional<std::string> ReadCode(const std::string& email, const std::string& purpose) override {
        if (RedisMgr::GetInstance()->EvalNumber(
            "local n=redis.call('INCR',KEYS[1]); if n==1 then redis.call('EXPIRE',KEYS[1],600) end; return n",
            (purpose == "reset_password" ? "code_attempt_reset_" : "code_attempt_") + email, "") > 8) return std::nullopt;
		std::string code;
		if (!RedisMgr::GetInstance()->Get((purpose == "reset_password" ? "code_reset_" : CODEPREFIX) + email, code)) {
			return std::nullopt;
		}
		return code;
	}
    /** @brief 比较并消费匹配验证码；过期、替换或已被消费时返回 false。 */
    bool ConsumeCode(const std::string& email, const std::string& code, const std::string& purpose) override {
        return RedisMgr::GetInstance()->EvalNumber(
            "if redis.call('GET',KEYS[1])==ARGV[1] then return redis.call('DEL',KEYS[1]) end; return 0",
            (purpose == "reset_password" ? "code_reset_" : CODEPREFIX) + email, code) == 1;
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
		const std::string& password,
        const std::string& email) override {
        return MysqlMgr::GetInstance()->UpdatePassword(username, password, email,
            /** @brief 在账号锁内先提升认证代次并撤销旧 Token；依赖失败阻止改密。 */ [](int uid) {
            return RedisMgr::GetInstance()->EvalNumber(
                "redis.call('INCR','auth_version_'..KEYS[1]); redis.call('DEL',KEYS[1]); return 1",
                std::to_string(uid), "") == 1;
        });
	}

    /** @brief 校验邮箱及密码，仅返回认证 UID；校验失败返回 nullopt。 */
	std::optional<gate::internal::UserRecord> CheckCredentials(
		const std::string& email,
		const std::string& password) override {
		UserInfo user;
		if (!MysqlMgr::GetInstance()->CheckPassword(email, password, user)) {
			return std::nullopt;
		}
        const auto version = RedisMgr::GetInstance()->EvalNumber(
            "return tonumber(redis.call('GET',KEYS[1]) or '0')", "auth_version_" + std::to_string(user.uid), "");
        return gate::internal::UserRecord{user.uid, version, std::move(user.credential_lease)};
	}
};

/** @brief 将选服端口适配到共享 Status gRPC 客户端。 */
class GrpcStatusAdapter final : public gate::internal::StatusPort {
public:
    /** @brief 匹配当前凭据才撤销；缺失视为已退出，旧凭据不能删除新登录。 */
    bool Revoke(int uid, const std::string& token) override {
        return RedisMgr::GetInstance()->EvalNumber(
            "local current=redis.call('HGET',KEYS[1],'utoken_'..KEYS[1]); "
            "if not current then return 1 end; if current~=ARGV[1] then return 0 end; "
            "redis.call('INCR','auth_version_'..KEYS[1]); redis.call('DEL',KEYS[1]); return 1",
            std::to_string(uid), token) == 1;
    }
    /** @brief 同步获取选服响应并复制业务码、Token 和地址，不额外推断成功。 */
	gate::internal::StatusAssignment Assign(int uid, long long auth_version) override {
		const auto response = StatusGrpcClient::GetInstance()->GetChatServer(uid, auth_version);
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
