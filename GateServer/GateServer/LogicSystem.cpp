#include "LogicSystem.h"

#include "GateRequestProduction.h"
#include "GateResponse.h"
#include "HttpConnection.h"

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
	_get_handlers.insert(make_pair(url, handler));
}

void LogicSystem::RegPost(std::string url, HttpHandler handler) {
	_post_handlers.insert(make_pair(url, handler));
}

LogicSystem::LogicSystem()
	: _gate_request(gate::CreateProductionGateRequest()) {
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

	RegPost("/get_varifycode", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::GetVarifyCode,
			[this](const Json::Value& request) {
				return _gate_request->Handle(gate::Endpoint::GetVarifyCode, request);
			});
	});

	RegPost("/user_register", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserRegister,
			[this](const Json::Value& request) {
				return _gate_request->Handle(gate::Endpoint::UserRegister, request);
			});
	});

	RegPost("/reset_pwd", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::ResetPassword,
			[this](const Json::Value& request) {
				return _gate_request->Handle(gate::Endpoint::ResetPassword, request);
			});
	});

	RegPost("/user_login", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserLogin,
			[this](const Json::Value& request) {
				return _gate_request->Handle(gate::Endpoint::UserLogin, request);
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
