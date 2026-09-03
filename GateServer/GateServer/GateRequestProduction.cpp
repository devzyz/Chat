#include "GateRequestProduction.h"

#include "GateRequestInternal.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "VerifyGrpcClient.h"
#include "const.h"

#include <optional>

namespace {

class GrpcVerificationAdapter final : public gate::internal::VerificationPort {
public:
	int RequestCode(const std::string& email) override {
		return VerifyGrpcClient::GetInstance()->GetVarifyCode(email).error();
	}
};

class RedisCodeStoreAdapter final : public gate::internal::CodeStore {
public:
	std::optional<std::string> ReadCode(const std::string& email) override {
		std::string code;
		if (!RedisMgr::GetInstance()->Get(CODEPREFIX + email, code)) {
			return std::nullopt;
		}
		return code;
	}
};

class MysqlUserStoreAdapter final : public gate::internal::UserStore {
public:
	int CreateUser(
		const std::string& username,
		const std::string& email,
		const std::string& password) override {
		return MysqlMgr::GetInstance()->RegUser(username, email, password);
	}

	bool IdentityMatches(
		const std::string& username,
		const std::string& email) override {
		return MysqlMgr::GetInstance()->CheckEmail(username, email);
	}

	bool UpdatePassword(
		const std::string& username,
		const std::string& password) override {
		return MysqlMgr::GetInstance()->UpdatePassword(username, password);
	}

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

class GrpcStatusAdapter final : public gate::internal::StatusPort {
public:
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
