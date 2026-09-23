#include "LogicSystem.h"

#include "GateResponse.h"
#include "HttpConnection.h"

/** @brief 登记路径对应的 GET 请求处理器，供后续请求分发。 */
void LogicSystem::RegisterGetHandler(std::string url, HttpHandler handler) {
	_get_handlers.insert(make_pair(url, handler));
}

/** @brief 登记路径对应的 POST 请求处理器，供后续请求分发。 */
void LogicSystem::RegisterPostHandler(std::string url, HttpHandler handler) {
	_post_handlers.insert(make_pair(url, handler));
}

LogicSystem::LogicSystem(gate::GateRequest& gate_request)
	: _gate_request(gate_request) {
	// 将请求体交给端点处理器，并写入统一 JSON 响应。
	const auto write_gate_response = /** @brief 解析正文并通过 Gate 端点合同生成统一 JSON 响应。 */ [](
		const std::shared_ptr<HttpConnection>& connection,
		gate::Endpoint endpoint,
		const gate::EndpointHandler& handler) {
		const auto body = boost::beast::buffers_to_string(connection->_request.body().data());
		connection->_response.set(http::field::content_type, "application/json");
		beast::ostream(connection->_response.body()) <<
			gate::HandleJsonRequest(endpoint, body, handler);
	};

	// 回显测试路径收到的查询参数。
	RegisterGetHandler("/get_test", /** @brief 回显测试路由收到的查询参数。 */ [](std::shared_ptr<HttpConnection> connection) {
		beast::ostream(connection->_response.body()) << "receive get_test req" << std::endl;
		int i = 0;
		for (auto& elem : connection->_get_params) {
			i++;
			beast::ostream(connection->_response.body()) << "param " << i << " : key = "
				<< elem.first << ", value = " << elem.second << std::endl;
		}
	});

	// 分发验证码请求并返回业务结果。
	/** @brief 把验证码 HTTP 请求交给共享响应流程。 */
	RegisterPostHandler("/get_varifycode", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::GetVarifyCode,
			// 执行验证码业务。
			/** @brief 执行验证码端点业务。 */ [this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::GetVarifyCode, request);
			});
	});

	// 分发注册请求并返回业务结果。
	/** @brief 把注册 HTTP 请求交给共享响应流程。 */
	RegisterPostHandler("/user_register", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserRegister,
			// 执行用户注册业务。
			/** @brief 执行用户注册端点业务。 */ [this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::UserRegister, request);
			});
	});

	// 分发密码重置请求并返回业务结果。
	/** @brief 把重置密码 HTTP 请求交给共享响应流程。 */
	RegisterPostHandler("/reset_pwd", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::ResetPassword,
			// 执行密码重置业务。
			/** @brief 执行密码重置端点业务。 */ [this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::ResetPassword, request);
			});
	});

	// 分发登录请求并返回业务结果。
	/** @brief 把登录 HTTP 请求交给共享响应流程。 */
	RegisterPostHandler("/user_login", [this, write_gate_response](std::shared_ptr<HttpConnection> connection) {
		write_gate_response(connection, gate::Endpoint::UserLogin,
			// 执行登录与选服业务。
			/** @brief 执行密码校验及选服端点业务。 */ [this](const Json::Value& request) {
				return _gate_request.Handle(gate::Endpoint::UserLogin, request);
			});
	});
}

/** @brief 按 GET 路由查询处理器并执行，返回是否找到对应路由。 */
bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_get_handlers.find(path) == _get_handlers.end()) {
		return false;
	}
	_get_handlers[path](con);
	return true;
}

/** @brief 按 POST 路由查询处理器并执行，返回是否找到对应路由。 */
bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_post_handlers.find(path) == _post_handlers.end()) {
		return false;
	}
	_post_handlers[path](con);
	return true;
}
