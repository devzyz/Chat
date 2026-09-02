#pragma once

#include "GateRequest.h"

#include <memory>
#include <optional>
#include <string>

namespace gate::internal {

struct UserRecord {
	int uid = 0;
};

struct StatusAssignment {
	int error = 1002;
	std::string token;
	std::string host;
	std::string port;
};

class VerificationPort {
public:
	virtual ~VerificationPort() = default;
	virtual int RequestCode(const std::string& email) = 0;
};

class CodeStore {
public:
	virtual ~CodeStore() = default;
	virtual std::optional<std::string> ReadCode(const std::string& email) = 0;
};

class UserStore {
public:
	virtual ~UserStore() = default;
	virtual int CreateUser(
		const std::string& username,
		const std::string& email,
		const std::string& password) = 0;
	virtual bool IdentityMatches(
		const std::string& username,
		const std::string& email) = 0;
	virtual bool UpdatePassword(
		const std::string& username,
		const std::string& password) = 0;
	virtual std::optional<UserRecord> CheckCredentials(
		const std::string& email,
		const std::string& password) = 0;
};

class StatusPort {
public:
	virtual ~StatusPort() = default;
	virtual StatusAssignment Assign(int uid) = 0;
};

std::unique_ptr<GateRequest> CreateGateRequest(
	std::shared_ptr<VerificationPort> verification,
	std::shared_ptr<CodeStore> code_store,
	std::shared_ptr<UserStore> user_store,
	std::shared_ptr<StatusPort> status);

} // namespace gate::internal
