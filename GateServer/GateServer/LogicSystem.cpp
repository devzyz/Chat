#include "LogicSystem.h"
#include "GateResponse.h"
#include "HttpConnection.h"
#include "VerifyGrpcClient.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
	_get_handlers.insert(make_pair(url, handler));
}

void LogicSystem::RegPost(std::string url, HttpHandler handler) {
	_post_handlers.insert(make_pair(url, handler));
}

LogicSystem::LogicSystem() {
	const auto write_gate_response = [](
		const std::shared_ptr<HttpConnection>& connection,
		gate::Endpoint endpoint,
		const gate::EndpointHandler& handler) {
		const auto body = boost::beast::buffers_to_string(connection->_request.body().data());
		connection->_response.set(http::field::content_type, "application/json");
		beast::ostream(connection->_response.body()) <<
			gate::HandleJsonRequest(endpoint, body, handler);
	};

	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		beast::ostream(connection->_response.body()) << "receive get_test req" << std::endl;
		int i = 0;
		for (auto& elem : connection->_get_params) {
			i++;
			beast::ostream(connection->_response.body()) << "param " << i << " : key = "
				<< elem.first << ", value = " << elem.second << std::endl;
		}
	});

	// 接收验证码的处理逻辑
	RegPost("/get_varifycode", [write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::GetVarifyCode,
			[](const Json::Value& request) {
				if (!request.isMember("email")) {
					SPDLOG_WARN("verification-code request missing email");
					return gate::Result{ErrorCodes::Error_Json};
				}

				const auto email = request["email"].asString();
				const auto response = VerifyGrpcClient::GetInstance()->GetVarifyCode(email);
				SPDLOG_DEBUG("verification-code RPC completed, email_size={}", email.size());
				return gate::Result{response.error()};
			});
	});

	// 注册的处理逻辑
	RegPost("/user_register", [write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserRegister,
			[](const Json::Value& request) {
				const auto email = request["email"].asString();
				const auto username = request["user"].asString();
				const auto password = request["passwd"].asString();
				const auto confirm = request["confirm"].asString();

				if (password != confirm) {
					SPDLOG_WARN("registration password confirmation mismatch");
					return gate::Result{ErrorCodes::PasswdErr};
				}

				std::string verification_code;
				if (!RedisMgr::GetInstance()->Get(CODEPREFIX + email, verification_code)) {
					SPDLOG_WARN("registration verification code expired");
					return gate::Result{ErrorCodes::VarifyExpired};
				}
				if (verification_code != request["varifycode"].asString()) {
					SPDLOG_WARN("registration verification code mismatch");
					return gate::Result{ErrorCodes::VarifyCodeErr};
				}

				const auto uid = MysqlMgr::GetInstance()->RegUser(username, email, password);
				if (uid == 0 || uid == -1) {
					SPDLOG_WARN("registration rejected because user or email exists");
					return gate::Result{ErrorCodes::UserExist};
				}
				return gate::Result{ErrorCodes::Success};
			});
		});

		RegPost("/reset_pwd", [write_gate_response](std::shared_ptr<HttpConnection> connection) {
			write_gate_response(connection, gate::Endpoint::ResetPassword,
				[](const Json::Value& request) {
					const auto email = request["email"].asString();
					const auto user = request["user"].asString();
					const auto password = request["password"].asString();
					const auto verification = request["varify"].asString();

					std::string verification_code;
					if (!RedisMgr::GetInstance()->Get(CODEPREFIX + email, verification_code)) {
						SPDLOG_WARN("password-reset verification code expired");
						return gate::Result{ErrorCodes::VarifyExpired};
					}
					if (verification_code != verification) {
						SPDLOG_WARN("password-reset verification code mismatch");
						return gate::Result{ErrorCodes::VarifyCodeErr};
					}
					if (!MysqlMgr::GetInstance()->CheckEmail(user, email)) {
						SPDLOG_WARN("password-reset username and email mismatch");
						return gate::Result{ErrorCodes::EmailNotMatch};
					}
					if (!MysqlMgr::GetInstance()->UpdatePassword(user, password)) {
						SPDLOG_ERROR("password-reset database update failed");
						return gate::Result{ErrorCodes::PasswdUpFailed};
					}

					SPDLOG_INFO("password reset succeeded");
					return gate::Result{ErrorCodes::Success};
				});
			});

		RegPost("/user_login", [write_gate_response](std::shared_ptr<HttpConnection> connection) {
			write_gate_response(connection, gate::Endpoint::UserLogin,
				[](const Json::Value& request) {
					const auto email = request["email"].asString();
					const auto password = request["password"].asString();
					UserInfo user_info;
					if (!MysqlMgr::GetInstance()->CheckPassword(email, password, user_info)) {
						SPDLOG_WARN("login credentials rejected");
						return gate::Result{ErrorCodes::PasswdInvalid};
					}

					const auto reply = StatusGrpcClient::GetInstance()->GetChatServer(user_info.uid);
					if (reply.error()) {
						SPDLOG_ERROR("chat server selection RPC failed, error={}", reply.error());
						return gate::Result{ErrorCodes::RPCFailed};
					}

					SPDLOG_INFO("login succeeded, uid={}", user_info.uid);
					return gate::Result{
						ErrorCodes::Success,
						user_info.uid,
						reply.token(),
						reply.host(),
						reply.port(),
					};
				});
			});
}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_get_handlers.find(path) == _get_handlers.end()) {
		return false;
	}
	_get_handlers[path](con);
	return true;
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_post_handlers.find(path) == _post_handlers.end()) {
		return false;
	}
	_post_handlers[path](con);
	return true;
}
