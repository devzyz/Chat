#pragma once

#include <memory>
#include <string>

struct AssignmentResult {
	int error = 1002;
	std::string host;
	std::string port;
	std::string token;
};

struct LoginResult {
	int error = 1002;
	int uid = 0;
	std::string token;
};

class StatusRouting {
public:
	virtual ~StatusRouting() = default;

	virtual AssignmentResult Assign(int uid) = 0;
	virtual LoginResult Validate(int uid, const std::string& token) = 0;
};
