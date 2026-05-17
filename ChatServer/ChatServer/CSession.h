#pragma once
#include <boost/asio.hpp>
#include <atomic>
#include "const.h"
#include "MsgNode.h"
#include <queue>
#include <mutex>

class LogicSystem;
class CServer;
class CSession : std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& ioc, CServer * server);
	~CSession();

	boost::asio::ip::tcp::socket& GetSocket();
	void Start();
	std::string& GetUuid();
	void Close();
	void Send(const char* msg, short msg_id, short msg_len);
	void Send(const std::string& msg, short msg_id);
private:
	/**
	 * @brief 
	 * @param head_total_len 
	 * 异步读取完整的包头
	 */
	void AsyncReadHead(std::size_t head_total_len);
	/**
	 * @brief 
	 * @param body_total_len 
	 * 异步读取完整的包体
	 */
	void AsyncReadBody(std::size_t body_total_len);
	/**
	 * @brief 
	 * @param maxLength 
	 * @param handler 
	 * 异步读取完整的成都maxLength
	 */
	void asyncReadFull(std::size_t maxLength,
		std::function<void(const boost::system::error_code& ec, std::size_t bytestransferred)> handler);
	/**
	 * @brief 
	 * @param read_len 当前已读字节
	 * @param total_len 总字节
	 * @param handler 回调函数
	 * 异步读取完整的total_len长度字节的数据
	 */
	void asyncReadLen(std::size_t read_len, std::size_t total_len,
		std::function<void(const boost::system::error_code& ec, std::size_t bytestransferred)> handler);

	/**
	 * @brief 
	 * @param ec 
	 * @param self 
	 * 异步写的回调函数
	 */
	void HandleWrite(const boost::system::error_code& ec, std::shared_ptr<CSession> self);

	boost::asio::ip::tcp::socket _socket;
	CServer* _server;
	std::string _uuid;
	char _data[MAX_LENGTH];

	// 收到的消息体
	std::shared_ptr<RecvNode> _recv_msg_node;
	// 当前包头部是否处理完成
	bool _b_head_parse;
	// 收到的头部
	std::shared_ptr<MsgNode> _recv_head_node;

	// 异步发送队列，保证发送的异步有序性
	std::queue<std::shared_ptr<SendNode>> _send_que;
	std::mutex _send_mutex;

	// 用于标记当前session有没有背关闭
	std::atomic<bool> _b_close;
};

class LogicNode {
public:
	LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode);
	~LogicNode();

	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recv_msg_node;
};

