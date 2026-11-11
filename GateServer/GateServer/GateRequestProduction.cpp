#include "GateRequestProduction.h"

#include "GateRequestInternal.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include <boost/algorithm/string/trim.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "GateRouting.h"
#include <sstream>
#include <vector>
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
                (purpose == "reset_password" ? "code_attempt_reset_" : "code_attempt_") + email,
                "") > 8)
            return std::nullopt;
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
                   (purpose == "reset_password" ? "code_reset_" : CODEPREFIX) + email,
                   code) == 1;
    }
};

/** @brief 将账号端口适配到共享 MysqlMgr，不缓存请求或凭据。 */
class MysqlUserStoreAdapter final : public gate::internal::UserStore {
public:
    /** @brief 转交账号创建并保留管理器的 UID 或失败哨兵返回值。 */
    int CreateUser(const std::string& username, const std::string& email, const std::string& password) override {
        return MysqlMgr::GetInstance()->RegUser(username, email, password);
    }

    /** @brief 查询用户名与邮箱是否匹配，保留管理器的失败结果。 */
    bool IdentityMatches(const std::string& username, const std::string& email) override {
        return MysqlMgr::GetInstance()->CheckEmail(username, email);
    }

    /** @brief 同步更新指定用户密码，直接返回管理器结果。 */
    bool UpdatePassword(const std::string& username, const std::string& password, const std::string& email) override {
        /**
         * 在账号锁内先提升认证代次并撤销旧 Token；
         * 依赖失败阻止改密。
         */
        return MysqlMgr::GetInstance()->UpdatePassword(username, password, email, [](int uid) {
            return RedisMgr::GetInstance()->EvalNumber(
                       "redis.call('INCR','auth_version_'..KEYS[1]); redis.call('DEL',KEYS[1]); return 1",
                       std::to_string(uid),
                       "") == 1;
        });
    }

    /** @brief 校验邮箱及密码，仅返回认证 UID；校验失败返回 nullopt。 */
    std::optional<gate::internal::UserRecord> CheckCredentials(const std::string& email, const std::string& password)
        override {
        UserInfo user;
        if (!MysqlMgr::GetInstance()->CheckPassword(email, password, user)) {
            return std::nullopt;
        }
        const auto version = RedisMgr::GetInstance()->EvalNumber(
            "return tonumber(redis.call('GET',KEYS[1]) or '0')",
            "auth_version_" + std::to_string(user.uid),
            "");
        return gate::internal::UserRecord{user.uid, version, std::move(user.credential_lease)};
    }
};

/** @brief 读取 Chat 租约与计数，通过代次检查发布凭据。 */
class AssignmentStore final : public gate_routing::AssignmentStore {
public:
    /** @brief 租约缺失或计数不可读时跳过该实例。 */
    std::optional<std::string> ReadCount(const std::string& name) override {
        auto redis = RedisMgr::GetInstance();
        std::string value;
        if (!redis->Get("chatlease_" + name, value) || !redis->HGet("logincount", name, value))
            return {};
        return value;
    }
    /** @brief 仅当前代次可覆盖凭据。 */
    bool PutToken(int uid, const std::string& token, long long version) override {
        return RedisMgr::GetInstance()->PutExpiringToken(uid, token, version);
    }
};

/** @brief 每次签发使用独立 UUID 生成器，可由多个登录请求并发调用。 */
class UuidTokenSource final : public gate_routing::TokenSource {
public:
    /** @brief 生成新的随机登录凭据。 */
    std::string Next() override {
        return boost::uuids::to_string(boost::uuids::random_generator()());
    }
};

/** @brief 对配置的实例按在线租约与负载选服，原子发布和撤销凭据。 */
class RedisAssignmentAdapter final : public gate::internal::StatusPort {
public:
    /** @brief 读取经过启动校验的不可变候选表。 */
    RedisAssignmentAdapter() {
        auto& config = ConfigMgr::GetInstance();
        std::stringstream names(config["ChatServers"]["Name"]);
        std::vector<RoutingServer> servers;
        std::string name;
        while (std::getline(names, name, ',')) {
            boost::algorithm::trim(name);
            auto section = config[name];
            servers.push_back({section["Name"], section["Host"], section["Port"]});
        }
        routing_ = gate_routing::CreateGateRouting(
            std::move(servers),
            std::make_shared<AssignmentStore>(),
            std::make_shared<UuidTokenSource>());
    }

    /** @brief 匹配当前凭据才撤销；缺失视为已退出，旧凭据不能删除新登录。 */
    bool Revoke(int uid, const std::string& token) override {
        return RedisMgr::GetInstance()->EvalNumber(
                   "local current=redis.call('HGET',KEYS[1],'utoken_'..KEYS[1]); "
                   "if not current then return 1 end; if current~=ARGV[1] then return 0 end; "
                   "redis.call('INCR','auth_version_'..KEYS[1]); redis.call('DEL',KEYS[1]); return 1",
                   std::to_string(uid),
                   token) == 1;
    }
    /** @brief 仅在找到有效实例并成功发布当前代次的凭据后返回地址。 */
    gate::internal::StatusAssignment Assign(int uid, long long auth_version) override {
        return routing_->Assign(uid, auth_version);
    }

private:
    std::unique_ptr<GateRouting> routing_;
};

} // namespace

namespace gate {

std::unique_ptr<GateRequest> CreateProductionGateRequest() {
    return internal::CreateGateRequest(
        std::make_shared<GrpcVerificationAdapter>(),
        std::make_shared<RedisCodeStoreAdapter>(),
        std::make_shared<MysqlUserStoreAdapter>(),
        std::make_shared<RedisAssignmentAdapter>());
}

} // namespace gate
