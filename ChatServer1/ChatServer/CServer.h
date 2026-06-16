#pragma once
#include <boost/asio.hpp>
#include <map>
#include <mutex>

class CSession;
/**
 * @brief 
 * Server类，用来管理所有的Session连接
 */
class CServer : public std::enable_shared_from_this<CServer>
{
public:
	CServer(boost::asio::io_context& ioc, short port);
	~CServer();
	// 清除根据某个session_id清除某个session
	void ClearSession(std::string session_id);
	bool CheckSessionValid(std::string session_id);
	void on_timer(const boost::system::error_code& e);
	// 后置初始化，保证shared_from_this已经存在
	void init();
	// io_context stop前的处理
	void stop();
private:
	void StartAcceptor();
	void HandleAcceptor(std::shared_ptr<CSession> new_session, const boost::system::error_code& error);
	boost::asio::io_context& _ioc;
	short _port;
	boost::asio::ip::tcp::acceptor _acceptor;
	// 用来根据session_id管理所有的session
	std::map<std::string, std::shared_ptr<CSession>> _sessions;
	std::mutex _mutex; // 保证互斥访问map

	// 定时检测器，每隔一段时间，判断一下客户端的心跳时间间隔是否正确
	// 如果不正确，代表客户端异常，则直接断开与客户端的连接
	boost::asio::steady_timer _timer;
};

