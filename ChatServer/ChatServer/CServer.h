#pragma once
#include <boost/asio.hpp>
#include "ChatSessionState.h"
#include <mutex>
#include <vector>

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
	void ClearSession(const ChatSessionState::Handle& session);
	bool CheckSessionValid(const ChatSessionState::Handle& session);
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
	std::vector<std::pair<ChatSessionState::Handle, std::shared_ptr<CSession>>> _sessions;
	std::shared_ptr<ChatSessionState> _session_state;
	std::mutex _mutex;

	// 定时检测器，每隔一段时间，判断一下客户端的心跳时间间隔是否正确
	// 如果不正确，代表客户端异常，则直接断开与客户端的连接
	boost::asio::steady_timer _timer;
};
