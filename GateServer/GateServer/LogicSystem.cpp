#include "LogicSystem.h"

#include "GateResponse.h"
#include "HttpConnection.h"

void LogicSystem::RegisterGetHandler(std::string url, HttpHandler handler) {
	_get_handlers.insert(make_pair(url, handler));
}

void LogicSystem::RegisterPostHandler(std::string url, HttpHandler handler) {
	_post_handlers.insert(make_pair(url, handler));
}

LogicSystem::LogicSystem(gate::GateRequest& gate_request)
	: _gate_request(gate_request) {
	// 将请求体交给端点处理器，并写入统一 JSON 响应。
	const auto write_gate_response = [](
		const std::shared_ptr<HttpConnection>& connection,
		gate::Endpoint endpoint,
		const gate::EndpointHandler& handler) {
		const auto body = boost::beast::buffers_to_string(connection->_request.body().data());
		connection->_response.set(http::field::content_type, "application/json");
		beast::ostream(connection->_response.body()) <<
			gate::HandleJsonRequest(endpoint, body, handler);
	};

	// 回显测试路径收到的查询参数。
	RegisterGetHandler("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		beast::ostream(connection->_response.body()) << "receive get_test req" << std::endl;
		int i = 0;
		for (auto& elem : connection->_get_params) {
			i++;
			beast::ostream(connection->_response.body()) << "param " << i << " : key = "
				<< elem.first << ", value = " << elem.second << std::endl;
		}
	});

	// 分发验证码请求并返回业务结果。
	RegisterPostHandler("/get_varifycode", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::GetVarifyCode,
			// 执行验证码业务。
			[this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::GetVarifyCode, request);
			});
	});

	// 分发注册请求并返回业务结果。
	RegisterPostHandler("/user_register", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserRegister,
			// 执行用户注册业务。
			[this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::UserRegister, request);
			});
	});

	// 分发密码重置请求并返回业务结果。
	RegisterPostHandler("/reset_pwd", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::ResetPassword,
			// 执行密码重置业务。
			[this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::ResetPassword, request);
			});
	});

	// 分发登录请求并返回业务结果。
	RegisterPostHandler("/user_login", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserLogin,
			// 执行登录与选服业务。
			[this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::UserLogin, request);
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
