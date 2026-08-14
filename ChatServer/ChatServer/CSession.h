#pragma once
#include <boost/asio.hpp>
#include <atomic>
#include "Const.h"
#include "MsgNode.h"
#include <queue>
#include <mutex>

class LogicSystem;
class CServer;
/**
 * @brief 
 * 与tcp客户端通信的会话类
 */
class CSession : public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& ioc, std::shared_ptr<CServer> server);
	~CSession();

	boost::asio::ip::tcp::socket& GetSocket();
	void Start();
	/**
	 * @brief 
	 * @return 
	 * 每个session有唯一的一个id,可以有CServer管理，方便通过CServer将对应的Session移除
	 */
	std::string& GetSessionId();
	/**
	 * @brief 
	 * @param uid 
	 * 每个session会跟一个tcp客户端建立通信，这里用来设置session对应的tcp客户端的uid
	 */
	void SetUserId(int uid);
	/**
	 * @brief 
	 * @return 
	 * 获取当前session对应的tcp客户端的uid
	 */
	int GetUserId();
	void Close();
	void Send(const char* msg, short msg_id, short msg_len);
	void Send(const std::string& msg, short msg_id);
	// 检测与当前session连接的客户端的心跳是否正确,正确返回true,否则返回false
	bool CheckHeartBeatAccurate(std::time_t& now);
	// 更新当前的心跳时间
	void UpdateHeartBeat();
	/**
	 * @brief
	 * 处理异常的session链接，因为服务器踢人是通过通知客户端，由客户端断开链接的，当出现异常的session链接后
	 * 可能是服务器踢人导致的，或者是出现了异常，不管哪种情况，都需要删除其session链接
	 */
	void DealExceptionSession();
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
	std::shared_ptr<CServer> _server;
	// 当前session的标识id
	std::string _session_id;
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
	std::atomic<bool> _b_stop;

	// 用于保存当前session连接的哪一个tcp客户端uid
	int _user_uid;

	// 上次接受数据的时间，包括正常发送的数据以及心跳包
	std::atomic<std::time_t> _last_heart_beat;

	// 访问session的锁
	std::mutex _session_mutex;
};

class LogicNode {
public:
	LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode);
	~LogicNode();

	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recv_msg_node;
};

