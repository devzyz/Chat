#include "HttpConnection.h"
#include "LogicSystem.h"

HttpConnection::HttpConnection(boost::asio::io_context& ioc) : _socket(ioc){

}

tcp::socket& HttpConnection::GetSocket() {
	return _socket;
}

/**
 * @brief 
 * 异步读取客户端的请求，同时进行处理和开启定时器
 */
void HttpConnection::Start() {
	auto self = shared_from_this();
	http::async_read(_socket, _buffer, _request, [self](beast::error_code ec, std::size_t bytes_transferred) {
		try {
			if (ec) {
				std::cout << "http read err is " << ec.what() << std::endl;
				return;
			}

			boost::ignore_unused(bytes_transferred);
			self->HandleReq();
			self->CheckDeadline();
		}
		catch (std::exception& e) {
			std::cout << "exception : " << e.what                                                                               () << std::endl;
		}
		});
}

// 数字转16进制
unsigned char ToHex(unsigned char x) {
	return x > 9 ? x + 55 : x + 48;
}

// 16进制转数字
unsigned char FromHex(unsigned char x) {
	unsigned char y;
	if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
	else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
	else if (x >= '0' && x <= '9') y = x - '0';
	else assert(0);
	return y;
}

// 生成可发送的请求串
std::string UrlEncode(const std::string& str) {
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++) {
		// 判断是否仅由数字和字母组成
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~') || 
			(str[i] == '=') || 
			(str[i] == '?')) {
			strTemp += str[i];
		}
		else if (str[i] == ' ') {
			strTemp += "+";
		}
		else {
			// 其他字符需要提前加%并且高四位和底四位分别转为16进制
			strTemp += "%";
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}

	return strTemp;
}

// 还原为原来的请求串
std::string UrlDecode(const std::string& str) {
	std::string strTemp = "";
	size_t length = str.length();

	for (size_t i = 0; i < length; i++) {
		// 还原+为空格
		if (str[i] == '+') strTemp += " ";
		// 遇到%则将后面的
		else if (str[i] == '%') {
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}

	return strTemp;
}

/**
 * @brief 处理url请求，从url请求中，提取出对应的参数
 * 修改_get_url为请求连接
 * 修改_get_params为连接附带的参数
 */
void HttpConnection::PreParseGetParam() {
	// 提取URL http://localhost:8080/get_test?key1=value1&key2=value2
	auto url = _request.target();
	// 查询?的位置
	auto query_pos = url.find('?');
	if (query_pos == std::string::npos) {
		_get_url = url;
		return;
	}

	_get_url = url.substr(0, query_pos);
	std::string query_string = url.substr(query_pos + 1);
	std::string key;
	std::string value;
	size_t pos = 0;
	while ((pos = query_string.find('&')) != std::string::npos) {
		auto pair = query_string.substr(0, pos);
		size_t eq_pos = pair.find('=');
		if (eq_pos != std::string::npos) {
			key = UrlDecode(pair.substr(0, eq_pos));
			value = UrlDecode(pair.substr(eq_pos + 1));
			_get_params[key] = value;
		}

		query_string.erase(0, pos + 1);
	}

	// 处理最后一个参数对
	if (!query_string.empty()) {
		size_t eq_pos = query_string.find('=');
		if (eq_pos != std::string::npos) {
			key = UrlDecode(query_string.substr(0, eq_pos));
			value = UrlDecode(query_string.substr(eq_pos + 1));
			_get_params[key] = value;
		}
	}
}

void HttpConnection::HandleReq() {
	// 设置版本 http/1.0 等
	_response.version(_request.version());
	// http维持长连接
	_response.keep_alive(false);

	// 处理get请求
	if (_request.method() == http::verb::get) {
		PreParseGetParam();
		bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
		if (!success) {
			// 失败原因
			_response.result(http::status::not_found);
			// 设置的回应头，文本类型、二进制类型等
			_response.set(http::field::content_type, "text/plain");

			beast::ostream(_response.body()) << "url not found\r\n";
			WriteResponse();
			return;
		}

		_response.result(http::status::ok);
		_response.set(http::field::server, "GateServer");
		WriteResponse();
		return;
	}

	if (_request.method() == http::verb::post) {
		bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
		if (!success) {
			// 失败原因
			_response.result(http::status::not_found);
			// 设置的回应头，文本类型、二进制类型等
			_response.set(http::field::content_type, "text/plain");

			beast::ostream(_response.body()) << "url not found\r\n";
			WriteResponse();
			return;
		}

		_response.result(http::status::ok);
		_response.set(http::field::server, "GateServer");
		WriteResponse();
		return;
	}
}

void HttpConnection::WriteResponse() {
	auto self = shared_from_this();
	// 设置回复消息的长度，用于http粘包处理
	_response.content_length(_response.body().size());
	http::async_write(_socket, _response, [self](beast::error_code ec, std::size_t bytes_transferred) {
		// 发送完成后，关闭发送端，并取消关联的定时器
		self->_socket.shutdown(tcp::socket::shutdown_send, ec);
		self->deadline_.cancel();
		});
}
// 开启定时器
void HttpConnection::CheckDeadline() {
	auto self = shared_from_this();
	deadline_.async_wait([self](beast::error_code ec) {
		if (!ec) {
			self->_socket.close(ec);
		}
		});
}