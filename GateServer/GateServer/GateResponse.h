#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <json/value.h>

namespace gate {

enum class Endpoint {
	GetVarifyCode,
	UserRegister,
	ResetPassword,
	UserLogin,
};

struct Result {
	int error = 0;
	int uid = 0;
	std::string token;
	std::string host;
	std::string port;
};

using EndpointHandler = std::function<Result(const Json::Value& request)>;

// This is the Module's only external Interface. It parses one request, invokes
// one endpoint Adapter, and emits the reviewed public response envelope.
std::string HandleJsonRequest(
	Endpoint endpoint,
	std::string_view request_body,
	const EndpointHandler& handler) noexcept;

} // namespace gate
