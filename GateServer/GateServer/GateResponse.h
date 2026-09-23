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

/** @brief 保存 HTTP 请求处理的状态码与 JSON 响应值，供传输层组装响应。 */
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
/** @brief 解析并校验 HTTP JSON 请求，调用指定业务操作并整形响应；无效输入映射为约定错误。 */
std::string HandleJsonRequest(
	Endpoint endpoint,
	std::string_view request_body,
	const EndpointHandler& handler) noexcept;

} // namespace gate
