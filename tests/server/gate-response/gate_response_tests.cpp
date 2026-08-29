#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/reader.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "GateResponse.h"

namespace gate {

void PrintTo(Endpoint endpoint, std::ostream* output) {
	switch (endpoint) {
	case Endpoint::GetVarifyCode:
		*output << "GetVarifyCode";
		break;
	case Endpoint::UserRegister:
		*output << "UserRegister";
		break;
	case Endpoint::ResetPassword:
		*output << "ResetPassword";
		break;
	case Endpoint::UserLogin:
		*output << "UserLogin";
		break;
	}
}

} // namespace gate

namespace {

constexpr int kJsonError = 1001;
constexpr int kRpcFailed = 1002;
constexpr int kBusinessFailure = 1005;

constexpr const char* kPasswordMarker = "PLAN2503-PASSWORD-MARKER";
constexpr const char* kConfirmMarker = "PLAN2503-CONFIRM-MARKER";
constexpr const char* kCodeMarker = "PLAN2503-CODE-MARKER";
constexpr const char* kInboundTokenMarker = "PLAN2503-INBOUND-TOKEN-MARKER";
constexpr const char* kEmailMarker = "plan2503-email-marker@example.test";

const gate::Endpoint kNonLoginEndpoints[] = {
	gate::Endpoint::GetVarifyCode,
	gate::Endpoint::UserRegister,
	gate::Endpoint::ResetPassword,
};

const gate::Endpoint kAllEndpoints[] = {
	gate::Endpoint::GetVarifyCode,
	gate::Endpoint::UserRegister,
	gate::Endpoint::ResetPassword,
	gate::Endpoint::UserLogin,
};

std::string EndpointName(gate::Endpoint endpoint) {
	std::ostringstream output;
	gate::PrintTo(endpoint, &output);
	return output.str();
}

Json::Value ParseResponse(const std::string& body) {
	Json::Value response;
	Json::Reader reader;
	EXPECT_TRUE(reader.parse(body, response)) << body;
	EXPECT_TRUE(response.isObject()) << body;
	return response;
}

void ExpectExactKeys(const Json::Value& response, std::vector<std::string> expected) {
	auto actual = response.getMemberNames();
	std::sort(actual.begin(), actual.end());
	std::sort(expected.begin(), expected.end());
	EXPECT_EQ(actual, expected);
}

std::string RequestWithForbiddenFields() {
	Json::Value request;
	request["email"] = kEmailMarker;
	request["user"] = "PLAN2503-USER-MARKER";
	request["passwd"] = kPasswordMarker;
	request["password"] = kPasswordMarker;
	request["confirm"] = kConfirmMarker;
	request["varifycode"] = kCodeMarker;
	request["varify"] = kCodeMarker;
	request["token"] = kInboundTokenMarker;
	return request.toStyledString();
}

void ExpectNoInputMarkers(const std::string& text) {
	for (const auto* marker : {
		kPasswordMarker,
		kConfirmMarker,
		kCodeMarker,
		kInboundTokenMarker,
		kEmailMarker,
	}) {
		EXPECT_EQ(text.find(marker), std::string::npos) << marker << " found in: " << text;
	}
}

class ScopedLogCapture {
public:
	ScopedLogCapture()
		: previous_(spdlog::default_logger()),
		  sink_(std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_)),
		  logger_(std::make_shared<spdlog::logger>("gate-response-test", sink_)) {
		logger_->set_pattern("%v");
		logger_->set_level(spdlog::level::trace);
		spdlog::set_default_logger(logger_);
	}

	~ScopedLogCapture() {
		spdlog::set_default_logger(previous_);
	}

	std::string text() {
		logger_->flush();
		return stream_.str();
	}

private:
	std::shared_ptr<spdlog::logger> previous_;
	std::ostringstream stream_;
	std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink_;
	std::shared_ptr<spdlog::logger> logger_;
};

class NonLoginResponseTest : public testing::TestWithParam<gate::Endpoint> {};

TEST_P(NonLoginResponseTest, SuccessContainsOnlyError) {
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		[](const Json::Value&) { return gate::Result{}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), 0);
	ExpectNoInputMarkers(body);
}

TEST_P(NonLoginResponseTest, BusinessFailureContainsOnlyError) {
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		[](const Json::Value&) { return gate::Result{kBusinessFailure}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kBusinessFailure);
	ExpectNoInputMarkers(body);
}

INSTANTIATE_TEST_SUITE_P(
	Endpoints,
	NonLoginResponseTest,
	testing::ValuesIn(kNonLoginEndpoints),
	[](const testing::TestParamInfo<gate::Endpoint>& info) { return EndpointName(info.param); });

TEST(GateLoginResponseTest, SuccessUsesTheExactReviewedAllowlist) {
	const auto body = gate::HandleJsonRequest(
		gate::Endpoint::UserLogin,
		RequestWithForbiddenFields(),
		[](const Json::Value&) {
			return gate::Result{0, 42, "opaque-session-value", "127.0.0.1", "8090"};
		});

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error", "uid", "token", "host", "port"});
	EXPECT_EQ(response["error"].asInt(), 0);
	EXPECT_EQ(response["uid"].asInt(), 42);
	EXPECT_EQ(response["token"].asString(), "opaque-session-value");
	EXPECT_EQ(response["host"].asString(), "127.0.0.1");
	EXPECT_EQ(response["port"].asString(), "8090");
	ExpectNoInputMarkers(body);
}

TEST(GateLoginResponseTest, FailureContainsOnlyError) {
	const auto body = gate::HandleJsonRequest(
		gate::Endpoint::UserLogin,
		RequestWithForbiddenFields(),
		[](const Json::Value&) { return gate::Result{kBusinessFailure}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kBusinessFailure);
	ExpectNoInputMarkers(body);
}

class AllEndpointResponseTest : public testing::TestWithParam<gate::Endpoint> {};

TEST_P(AllEndpointResponseTest, MalformedJsonReturnsTheStableJsonError) {
	bool called = false;
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		"{not-json",
		[&called](const Json::Value&) {
			called = true;
			return gate::Result{};
		});

	EXPECT_FALSE(called);
	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kJsonError);
}

TEST_P(AllEndpointResponseTest, ForbiddenRequestFieldsAndValuesAreNeverReflected) {
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		[](const Json::Value&) { return gate::Result{kBusinessFailure}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	ExpectNoInputMarkers(body);
	for (const auto* field : {"email", "user", "passwd", "password", "confirm", "varifycode", "varify"}) {
		EXPECT_EQ(body.find(std::string("\"") + field + "\""), std::string::npos) << body;
	}
}

TEST_P(AllEndpointResponseTest, InternalExceptionReturnsStableErrorWithoutLoggingDetails) {
	ScopedLogCapture logs;
	const auto exception_text = std::string(kPasswordMarker) + " " + kConfirmMarker + " " +
		kCodeMarker + " " + kInboundTokenMarker + " " + kEmailMarker +
		" password confirm varifycode email token";
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		[&exception_text](const Json::Value&) -> gate::Result {
			throw std::runtime_error(exception_text);
		});

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kRpcFailed);
	ExpectNoInputMarkers(body);
	ExpectNoInputMarkers(logs.text());
	EXPECT_EQ(logs.text().find("password"), std::string::npos);
	EXPECT_EQ(logs.text().find("confirm"), std::string::npos);
	EXPECT_EQ(logs.text().find("varifycode"), std::string::npos);
	EXPECT_EQ(logs.text().find("email"), std::string::npos);
	EXPECT_EQ(logs.text().find("token"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
	Endpoints,
	AllEndpointResponseTest,
	testing::ValuesIn(kAllEndpoints),
	[](const testing::TestParamInfo<gate::Endpoint>& info) { return EndpointName(info.param); });

TEST(GateLoginResponseTest, RpcFailureUsesTheStableErrorEnvelope) {
	const auto body = gate::HandleJsonRequest(
		gate::Endpoint::UserLogin,
		RequestWithForbiddenFields(),
		[](const Json::Value&) { return gate::Result{kRpcFailed}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kRpcFailed);
	ExpectNoInputMarkers(body);
}

} // namespace
