#include "LogicSystem.h"
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
	RegPost("/get_varifycode", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		SPDLOG_DEBUG("verification-code request received, body_size={}", body_str.size());
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			SPDLOG_WARN("verification-code request JSON parse failed");
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		if (!src_root.isMember("email")) {
			SPDLOG_WARN("verification-code request missing email");
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		auto email = src_root["email"].asString();
		GetVarifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVarifyCode(email);
		SPDLOG_DEBUG("verification-code RPC completed, email_size={}", email.size());
		root["error"] = rsp.error();
		root["email"] = src_root["email"];
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;
	});

	// 注册的处理逻辑
	RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		SPDLOG_DEBUG("registration request received, body_size={}", body_str.size());
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root; // 返回的json数据
		Json::Reader reader; 
		Json::Value src_root; // 接收的json数据
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			SPDLOG_WARN("registration request JSON parse failed");
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		
		auto email = src_root["email"].asString();
		auto username = src_root["user"].asString();
		auto password = src_root["passwd"].asString();
		auto confirm = src_root["confirm"].asString();

		// 双保险验证，如果前端传过来的密码与验证码不匹配，则返回错误
		if (password != confirm) {
			SPDLOG_WARN("registration password confirmation mismatch");
			root["error"] = ErrorCodes::PasswdErr;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 先查找请求数据与redis内的验证码是否匹配
		std::string varify_code;
		bool b_get_varify = RedisMgr::GetInstance()->Get(CODEPREFIX + src_root["email"].asString(), varify_code);

		// 查找失败
		if (!b_get_varify) {
			SPDLOG_WARN("registration verification code expired");
			root["error"] = ErrorCodes::VarifyExpired;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 查找成功，但是验证码不匹配
		if (varify_code != src_root["varifycode"].asString()) {
			SPDLOG_WARN("registration verification code mismatch");
			root["error"] = ErrorCodes::VarifyCodeErr;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 查找数据库判断用户是否存在
		int uid = MysqlMgr::GetInstance()->RegUser(username, email, password);
		if (uid == 0 || uid == -1) {
			SPDLOG_WARN("registration rejected because user or email exists");
			root["error"] = ErrorCodes::UserExist;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 无错误
		root["error"] = 0;
		root["email"] = src_root["email"];
		root["uid"] = uid;
		root["user"] = src_root["user"].asString();
		root["passwd"] = src_root["passwd"].asString();
		root["confirm"] = src_root["confirm"].asString();
		root["varifycode"] = src_root["varifycode"].asString();
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;
		});

		RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {
			auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
			SPDLOG_DEBUG("password-reset request received, body_size={}", body_str.size());
			connection->_response.set(http::field::content_type, "text/json");
			Json::Value root;
			Json::Reader reader;
			Json::Value src_root;
			bool parse_success = reader.parse(body_str, src_root);
			if (!parse_success) {
				SPDLOG_WARN("password-reset request JSON parse failed");
				root["error"] = ErrorCodes::Error_Json;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			auto email = src_root["email"].asString();
			auto user = src_root["user"].asString();
			auto password = src_root["password"].asString();
			auto varify = src_root["varify"].asString();

			std::string varify_code;
			bool b_get_varify = RedisMgr::GetInstance()->Get(CODEPREFIX + email, varify_code);
			if (!b_get_varify) {
				SPDLOG_WARN("password-reset verification code expired");
				root["error"] = ErrorCodes::VarifyExpired;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			// 输入的验证码与redis内的验证码不匹配
			if (varify_code != varify) {
				SPDLOG_WARN("password-reset verification code mismatch");
				root["error"] = ErrorCodes::VarifyCodeErr;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			// 查询数据库，判断用户名和邮箱是否匹配
			bool email_valid = MysqlMgr::GetInstance()->CheckEmail(user, email);
			if (!email_valid) {
				SPDLOG_WARN("password-reset username and email mismatch");
				root["error"] = ErrorCodes::EmailNotMatch;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			bool b_update = MysqlMgr::GetInstance()->UpdatePassword(user, password);
			if (!b_update) {
				SPDLOG_ERROR("password-reset database update failed");
				root["error"] = ErrorCodes::PasswdUpFailed;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			SPDLOG_INFO("password reset succeeded");
			root["error"] = 0;
			root["email"] = email;
			root["user"] = user;
			root["password"] = password;
			root["varify"] = src_root["varify"].asString();
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
			});

		RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
			auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
			SPDLOG_DEBUG("login request received, body_size={}", body_str.size());

			connection->_response.set(http::field::content_type, "text/json");
			Json::Value root;
			Json::Reader reader;
			Json::Value src_root;
			bool parse_success = reader.parse(body_str, src_root); // 解析json数据，将解析后的数据保存到src_root中
			if (!parse_success) {
				SPDLOG_WARN("login request JSON parse failed");
				root["error"] = ErrorCodes::Error_Json;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			auto email = src_root["email"].asString();
			auto password = src_root["password"].asString();
			UserInfo userInfo;

			// 查询数据库，判断用户名和邮箱是否匹配
			bool password_valid = MysqlMgr::GetInstance()->CheckPassword(email, password, userInfo);
			if (!password_valid) {
				SPDLOG_WARN("login credentials rejected");
				root["error"] = ErrorCodes::PasswdInvalid;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			// 通过grpc找到负载均衡的chatserver
			auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
			if (reply.error()) {
				SPDLOG_ERROR("chat server selection RPC failed, error={}", reply.error());
				root["error"] = ErrorCodes::RPCFailed;
				std::string jsonstr = root.toStyledString();
				beast::ostream(connection->_response.body()) << jsonstr;
				return true;
			}

			SPDLOG_INFO("login succeeded, uid={}", userInfo.uid);
			root["error"] = 0;
			root["email"] = email;
			root["uid"] = userInfo.uid;
			root["token"] = reply.token();
			root["host"] = reply.host();
			root["port"] = reply.port();
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
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