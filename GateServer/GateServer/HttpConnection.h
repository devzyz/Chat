#pragma once
#include "const.h"

class LogicSystem;

/** @brief 拥有一个 HTTP 连接、请求缓冲和期限计时器，异步操作以共享引用保持存活。 */
class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
	friend class LogicSystem;
	friend class CServer;
public:
	/** @brief 初始化HttpConnection，拥有一个 HTTP 连接、请求缓冲和期限计时器，异步操作以共享引用保持存活。 */
	HttpConnection(boost::asio::io_context& ioc, std::shared_ptr<LogicSystem> logic);
	/**
	 * @brief 与客户端通信，读取客户端发来的数据
	 */
	void Start();
	/** @brief 返回连接内部 socket 的借用引用，调用方不得让引用超过连接生命周期。 */
	tcp::socket& GetSocket();
private:
	/** @brief 向 socket 执行器派发计时器取消与连接关闭，自身存活至派发回调结束。 */
	void Stop();
	/** @brief 等待连接期限并在到期时关闭当前 HTTP socket，捕获自身以覆盖异步完成。 */
	void CheckDeadline();
	/**
	 * @brief 与客户端通信，将数据发往客户端
	 */
	void WriteResponse();

	/**
	 * @brief 处理客户端发来的请求
	 */
	void HandleReq();
	
	/** @brief 从 GET target 提取路径与查询参数供路由和请求校验使用。 */
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
