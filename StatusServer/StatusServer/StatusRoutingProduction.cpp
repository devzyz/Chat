#include "StatusRoutingProduction.h"

#include "RedisMgr.h"
#include "const.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace {

/** @brief 复用 RedisMgr 读取实例计数并存取用户 Token，保留现有键格式。 */
class RedisStatusStore final : public status_routing_internal::StatusStore {
public:
    /** @brief 从登录计数哈希读取实例计数；HGet 返回 false 时返回未知值。 */
	std::optional<std::string> ReadCount(const std::string& server_name) override {
		std::string value;
		if (!RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name, value)) {
			return std::nullopt;
		}
		return value;
	}

    /** @brief 写入 UID 哈希的 Token 字段，直接传播 HSet 成功或失败。 */
	bool PutToken(int uid, const std::string& token) override {
		const auto uid_text = std::to_string(uid);
		return RedisMgr::GetInstance()->HSet(uid_text, USER_TOKEN_PREFIX + uid_text, token);
	}

    /** @brief 读取 UID 对应 Token；HGet 返回 false 时返回 nullopt。 */
	std::optional<std::string> GetToken(int uid) override {
		const auto uid_text = std::to_string(uid);
		std::string token;
		if (!RedisMgr::GetInstance()->HGet(uid_text, USER_TOKEN_PREFIX + uid_text, token)) {
			return std::nullopt;
		}
		return token;
	}
};

/** @brief 拥有 UUID 随机生成器，生成用于登录匹配的 Token。 */
class UuidTokenSource final : public status_routing_internal::TokenSource {
public:
    /** @brief 返回新 UUID 的字符串形式，生成异常交给路由层处理。 */
	std::string Next() override {
		return boost::uuids::to_string(generator_());
	}

private:
	boost::uuids::random_generator generator_;
};

} // namespace

std::unique_ptr<StatusRouting> CreateProductionStatusRouting(std::vector<RoutingServer> servers) {
	return status_routing_internal::CreateStatusRouting(
		std::move(servers),
		std::make_shared<RedisStatusStore>(),
		std::make_shared<UuidTokenSource>());
}
