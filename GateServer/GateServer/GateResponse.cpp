#include "GateResponse.h"

#include "const.h"

#include <exception>

#include <json/reader.h>
#include <spdlog/spdlog.h>

namespace gate {

namespace {

std::string ShapeResponse(Endpoint endpoint, const Result& result) {
	Json::Value response;
	response["error"] = result.error;
	if (endpoint == Endpoint::UserLogin && result.error == ErrorCodes::Success) {
		response["uid"] = result.uid;
		response["token"] = result.token;
		response["host"] = result.host;
		response["port"] = result.port;
	}
	return response.toStyledString();
}

} // namespace

std::string HandleJsonRequest(
	Endpoint endpoint,
	std::string_view request_body,
	const EndpointHandler& handler) noexcept {
	try {
		Json::Value request;
		Json::Reader reader;
		if (!reader.parse(std::string(request_body), request) || !request.isObject()) {
			SPDLOG_WARN("Gate request JSON parse failed");
			return ShapeResponse(endpoint, Result{ErrorCodes::Error_Json});
		}
		return ShapeResponse(endpoint, handler(request));
	}
	catch (const std::exception&) {
		SPDLOG_ERROR("Gate request processing failed");
	}
	catch (...) {
		SPDLOG_ERROR("Gate request processing failed");
	}

	return ShapeResponse(endpoint, Result{ErrorCodes::RPCFailed});
}

} // namespace gate
