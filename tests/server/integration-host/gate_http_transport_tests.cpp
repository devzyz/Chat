#include <gtest/gtest.h>

#include "../../../GateServer/GateServer/CServer.h"
#include "../../../GateServer/GateServer/GateRequest.h"
#include "../../../GateServer/GateServer/LogicSystem.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <chrono>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class RecordingGateRequest final : public gate::GateRequest {
public:
	gate::Result Handle(gate::Endpoint endpoint, const Json::Value&) override {
		std::lock_guard<std::mutex> lock(mutex_);
		endpoints_.push_back(endpoint);
		return {};
	}

	std::vector<gate::Endpoint> Endpoints() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return endpoints_;
	}

private:
	mutable std::mutex mutex_;
	std::vector<gate::Endpoint> endpoints_;
};

class T09_GHTTP_Core : public testing::Test {
protected:
	void SetUp() override {
		request_ = std::make_shared<RecordingGateRequest>();
		logic_ = std::make_shared<LogicSystem>(*request_);
		server_ = std::make_shared<CServer>(ioc_, "127.0.0.1", 0, logic_);
		server_->Start();
		server_thread_ = std::thread([this] { ioc_.run(); });
	}

	void TearDown() override {
		server_->Stop();
		ioc_.stop();
		if (server_thread_.joinable()) {
			server_thread_.join();
		}
	}

	http::response<http::dynamic_body> Post(
		const std::string& route,
		const std::string& body) {
		boost::asio::io_context client_ioc;
		beast::tcp_stream stream(client_ioc);
		stream.expires_after(2s);
		stream.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});

		http::request<http::string_body> request{http::verb::post, route, 11};
		request.set(http::field::host, "127.0.0.1");
		request.set(http::field::content_type, "application/json");
		request.body() = body;
		request.prepare_payload();
		http::write(stream, request);

		beast::flat_buffer buffer;
		http::response<http::dynamic_body> response;
		http::read(stream, buffer, response);
		return response;
	}

	boost::asio::io_context ioc_;
	std::shared_ptr<RecordingGateRequest> request_;
	std::shared_ptr<LogicSystem> logic_;
	std::shared_ptr<CServer> server_;
	std::thread server_thread_;
};

// T09-GHTTP-01
TEST_F(T09_GHTTP_Core, PublishesReadyLoopbackEndpointAndStopsIdempotently) {
	EXPECT_EQ(server_->BoundAddress(), "127.0.0.1");
	EXPECT_NE(server_->BoundPort(), 0);
	server_->Stop();
	server_->Stop();
}

// T09-GHTTP-02
TEST_F(T09_GHTTP_Core, VerificationRouteDelegatesToGateRequest) {
	EXPECT_EQ(Post("/get_varifycode", "{}").result(), http::status::ok);
	EXPECT_EQ(request_->Endpoints(), std::vector<gate::Endpoint>{gate::Endpoint::GetVarifyCode});
}

// T09-GHTTP-03
TEST_F(T09_GHTTP_Core, RegistrationRouteDelegatesToGateRequest) {
	EXPECT_EQ(Post("/user_register", "{}").result(), http::status::ok);
	EXPECT_EQ(request_->Endpoints(), std::vector<gate::Endpoint>{gate::Endpoint::UserRegister});
}

// T09-GHTTP-04
TEST_F(T09_GHTTP_Core, ResetRouteDelegatesToGateRequest) {
	EXPECT_EQ(Post("/reset_pwd", "{}").result(), http::status::ok);
	EXPECT_EQ(request_->Endpoints(), std::vector<gate::Endpoint>{gate::Endpoint::ResetPassword});
}

// T09-GHTTP-05
TEST_F(T09_GHTTP_Core, LoginRouteDelegatesToGateRequest) {
	EXPECT_EQ(Post("/user_login", "{}").result(), http::status::ok);
	EXPECT_EQ(request_->Endpoints(), std::vector<gate::Endpoint>{gate::Endpoint::UserLogin});
}

// T09-GHTTP-06
TEST_F(T09_GHTTP_Core, FragmentedMaximumBodyIsAcceptedExactlyOnce) {
	const std::string prefix = "{\"padding\":\"";
	const std::string suffix = "\"}";
	const std::string body = prefix +
		std::string(CServer::MaxRequestBodyBytes() - prefix.size() - suffix.size(), 'x') + suffix;

	boost::asio::io_context client_ioc;
	beast::tcp_stream stream(client_ioc);
	stream.expires_after(2s);
	stream.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});
	const std::string raw = "POST /user_login HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " +
		std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
	for (std::size_t offset = 0; offset < raw.size(); offset += 17) {
		boost::asio::write(stream.socket(), boost::asio::buffer(raw.data() + offset, (std::min)(std::size_t{17}, raw.size() - offset)));
	}
	beast::flat_buffer buffer;
	http::response<http::dynamic_body> response;
	http::read(stream, buffer, response);

	EXPECT_EQ(response.result(), http::status::ok);
	EXPECT_EQ(request_->Endpoints(), std::vector<gate::Endpoint>{gate::Endpoint::UserLogin});
}

// T09-GHTTP-07
TEST_F(T09_GHTTP_Core, OverLimitMalformedAndInterruptedRequestsNeverDispatch) {
	const std::string prefix = "{\"padding\":\"";
	const std::string suffix = "\"}";
	const auto over_limit = prefix +
		std::string(CServer::MaxRequestBodyBytes() + 1 - prefix.size() - suffix.size(), 'x') + suffix;

	boost::asio::io_context client_ioc;
	beast::tcp_stream oversized(client_ioc);
	oversized.expires_after(2s);
	oversized.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});
	http::request<http::string_body> request{http::verb::post, "/user_login", 11};
	request.body() = over_limit;
	request.prepare_payload();
	http::write(oversized, request);
	beast::flat_buffer buffer;
	http::response<http::dynamic_body> response;
	beast::error_code error;
	http::read(oversized, buffer, response, error);
	EXPECT_TRUE(error || response.result() == http::status::payload_too_large);

	beast::tcp_stream malformed(client_ioc);
	malformed.expires_after(2s);
	malformed.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});
	boost::asio::write(malformed.socket(), boost::asio::buffer("POST /user_login HTTP/1.1\r\nContent-Length: nope\r\n\r\n"));
	malformed.socket().shutdown(tcp::socket::shutdown_send, error);

	beast::tcp_stream interrupted(client_ioc);
	interrupted.expires_after(2s);
	interrupted.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});
	boost::asio::write(interrupted.socket(), boost::asio::buffer("POST /user_login HTTP/1.1\r\nContent-Length: 20\r\n\r\n{"));
	interrupted.socket().close(error);
	std::this_thread::sleep_for(100ms);

	EXPECT_TRUE(request_->Endpoints().empty());
	EXPECT_EQ(Post("/user_login", "{}").result(), http::status::ok);
}

} // namespace
