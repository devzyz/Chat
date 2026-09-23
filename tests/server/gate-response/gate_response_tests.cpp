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

/** 为参数化测试输出端点的稳定名称。 */
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

/** 将端点枚举转换为测试实例名称。 */
std::string EndpointName(gate::Endpoint endpoint) {
	std::ostringstream output;
	gate::PrintTo(endpoint, &output);
	return output.str();
}

/** 解析响应并断言为 JSON 对象。 */
Json::Value ParseResponse(const std::string& body) {
	Json::Value response;
	Json::Reader reader;
	EXPECT_TRUE(reader.parse(body, response)) << body;
	EXPECT_TRUE(response.isObject()) << body;
	return response;
}

/** 按排序后的键集合核对响应精确白名单。 */
void ExpectExactKeys(const Json::Value& response, std::vector<std::string> expected) {
	auto actual = response.getMemberNames();
	std::sort(actual.begin(), actual.end());
	std::sort(expected.begin(), expected.end());
	EXPECT_EQ(actual, expected);
}

/** 构造带可追踪敏感字段标记的请求以验证不反射。 */
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

/** 断言响应或日志中没有任何输入敏感标记。 */
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

/** 作用域内捕获默认日志器输出并在结束时恢复。 */
class ScopedLogCapture {
public:
	/** 安装写入内存流的测试日志器并保存原日志器。 */
	ScopedLogCapture()
		: previous_(spdlog::default_logger()),
		  sink_(std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_)),
		  logger_(std::make_shared<spdlog::logger>("gate-response-test", sink_)) {
		logger_->set_pattern("%v");
		logger_->set_level(spdlog::level::trace);
		spdlog::set_default_logger(logger_);
	}

	/** 恢复原默认日志器。 */
	~ScopedLogCapture() {
		spdlog::set_default_logger(previous_);
	}

	/** 刷新日志器后返回捕获文本。 */
	std::string Text() {
		logger_->flush();
		return stream_.str();
	}

private:
	std::shared_ptr<spdlog::logger> previous_;
	std::ostringstream stream_;
	std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink_;
	std::shared_ptr<spdlog::logger> logger_;
};

/** 覆盖所有非登录端点的参数化响应合同。 */
class NonLoginResponseTest : public testing::TestWithParam<gate::Endpoint> {};

/** 验证非登录成功响应仅含错误码且不反射输入。 */
TEST_P(NonLoginResponseTest, SuccessContainsOnlyError) {
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		/** 提供默认成功业务结果。 */ [](const Json::Value&) { return gate::Result{}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), 0);
	ExpectNoInputMarkers(body);
}

/** 验证非登录失败响应仅含错误码。 */
TEST_P(NonLoginResponseTest, BusinessFailureContainsOnlyError) {
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		/** 提供固定业务失败结果。 */ [](const Json::Value&) { return gate::Result{kBusinessFailure}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kBusinessFailure);
	ExpectNoInputMarkers(body);
}

INSTANTIATE_TEST_SUITE_P(
	Endpoints,
	NonLoginResponseTest,
	testing::ValuesIn(kNonLoginEndpoints),
	/** 使用端点名生成参数化用例名称。 */ [](const testing::TestParamInfo<gate::Endpoint>& info) { return EndpointName(info.param); });

/** 验证登录成功响应严格遵循身份、Token 及端点白名单。 */
TEST(GateLoginResponseTest, SuccessUsesTheExactReviewedAllowlist) {
	const auto body = gate::HandleJsonRequest(
		gate::Endpoint::UserLogin,
		RequestWithForbiddenFields(),
		/** 提供完整登录成功结果。 */ [](const Json::Value&) {
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

/** 验证登录业务失败仅公开错误码。 */
TEST(GateLoginResponseTest, FailureContainsOnlyError) {
	const auto body = gate::HandleJsonRequest(
		gate::Endpoint::UserLogin,
		RequestWithForbiddenFields(),
		/** 提供固定业务失败结果。 */ [](const Json::Value&) { return gate::Result{kBusinessFailure}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kBusinessFailure);
	ExpectNoInputMarkers(body);
}

/** 覆盖全部端点的 JSON 校验及信息边界。 */
class AllEndpointResponseTest : public testing::TestWithParam<gate::Endpoint> {};

/** 验证损坏 JSON 返回稳定错误且不调用业务处理器。 */
TEST_P(AllEndpointResponseTest, MalformedJsonReturnsTheStableJsonError) {
	bool called = false;
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		"{not-json",
		/** 记录业务处理器是否被错误地调用。 */ [&called](const Json::Value&) {
			called = true;
			return gate::Result{};
		});

	EXPECT_FALSE(called);
	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kJsonError);
}

/** 验证请求中的禁止字段及值不会被反射到响应。 */
TEST_P(AllEndpointResponseTest, ForbiddenRequestFieldsAndValuesAreNeverReflected) {
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		/** 提供固定业务失败结果。 */ [](const Json::Value&) { return gate::Result{kBusinessFailure}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	ExpectNoInputMarkers(body);
	for (const auto* field : {"email", "user", "passwd", "password", "confirm", "varifycode", "varify"}) {
		EXPECT_EQ(body.find(std::string("\"") + field + "\""), std::string::npos) << body;
	}
}

/** 验证内部异常映射为稳定错误且不在日志泄露细节。 */
TEST_P(AllEndpointResponseTest, InternalExceptionReturnsStableErrorWithoutLoggingDetails) {
	ScopedLogCapture logs;
	const auto exception_text = std::string(kPasswordMarker) + " " + kConfirmMarker + " " +
		kCodeMarker + " " + kInboundTokenMarker + " " + kEmailMarker +
		" password confirm varifycode email token";
	const auto body = gate::HandleJsonRequest(
		GetParam(),
		RequestWithForbiddenFields(),
		/** 抛出包含敏感夹具标记的异常以验证脱敏边界。 */ [&exception_text](const Json::Value&) -> gate::Result {
			throw std::runtime_error(exception_text);
		});

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kRpcFailed);
	ExpectNoInputMarkers(body);
	ExpectNoInputMarkers(logs.Text());
	EXPECT_EQ(logs.Text().find("password"), std::string::npos);
	EXPECT_EQ(logs.Text().find("confirm"), std::string::npos);
	EXPECT_EQ(logs.Text().find("varifycode"), std::string::npos);
	EXPECT_EQ(logs.Text().find("email"), std::string::npos);
	EXPECT_EQ(logs.Text().find("token"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
	Endpoints,
	AllEndpointResponseTest,
	testing::ValuesIn(kAllEndpoints),
	/** 使用端点名生成参数化用例名称。 */ [](const testing::TestParamInfo<gate::Endpoint>& info) { return EndpointName(info.param); });

/** 验证登录 RPC 失败保持稳定的错误响应结构。 */
TEST(GateLoginResponseTest, RpcFailureUsesTheStableErrorEnvelope) {
	const auto body = gate::HandleJsonRequest(
		gate::Endpoint::UserLogin,
		RequestWithForbiddenFields(),
		/** 提供固定 RPC 失败业务结果。 */ [](const Json::Value&) { return gate::Result{kRpcFailed}; });

	const auto response = ParseResponse(body);
	ExpectExactKeys(response, {"error"});
	EXPECT_EQ(response["error"].asInt(), kRpcFailed);
	ExpectNoInputMarkers(body);
}

} // namespace
