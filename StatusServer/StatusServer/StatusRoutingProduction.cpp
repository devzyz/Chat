#include "StatusRoutingProduction.h"

#include "RedisMgr.h"
#include "const.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace {

class RedisStatusStore final : public status_routing_internal::StatusStore {
public:
	std::optional<std::string> ReadCount(const std::string& server_name) override {
		std::string value;
		if (!RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name, value)) {
			return std::nullopt;
		}
		return value;
	}

	bool PutToken(int uid, const std::string& token) override {
		const auto uid_text = std::to_string(uid);
		return RedisMgr::GetInstance()->HSet(uid_text, USER_TOKEN_PREFIX + uid_text, token);
	}

	std::optional<std::string> GetToken(int uid) override {
		const auto uid_text = std::to_string(uid);
		std::string token;
		if (!RedisMgr::GetInstance()->HGet(uid_text, USER_TOKEN_PREFIX + uid_text, token)) {
			return std::nullopt;
		}
		return token;
	}
};

class UuidTokenSource final : public status_routing_internal::TokenSource {
public:
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
