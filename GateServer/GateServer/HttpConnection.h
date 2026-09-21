#pragma once
#include "const.h"

class LogicSystem;

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
	friend class LogicSystem;
	friend class CServer;
public:
	HttpConnection(boost::asio::io_context& ioc, std::shared_ptr<LogicSystem> logic);
	/**
	 * @brief 与客户端通信，读取客户端发来的数据
	 */
	void Start();
	tcp::socket& GetSocket();
private:
	void Stop();
	void CheckDeadline();
	/**
	 * @brief 与客户端通信，将数据发往客户端
	 */
	void WriteResponse();

	/**
	 * @brief 处理客户端发来的请求
	 */
	void HandleReq();
	
	void PreParseGetParam();
	tcp::socket _socket;
	beast::flat_buffer _buffer{ 8192 };
	http::request_parser<http::dynamic_body> _parser;
	http::request<http::dynamic_body> _request;
	http::response<http::dynamic_body> _response;
	net::steady_timer deadline_{
		_socket.get_executor(), std::chrono::seconds(60)
	};

	std::string _get_url;
	std::unordered_map<std::string, std::string> _get_params;
	std::shared_ptr<LogicSystem> _logic;
};
