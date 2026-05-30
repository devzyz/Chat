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
private:
	void StartAcceptor();
	void HandleAcceptor(std::shared_ptr<CSession> new_session, const boost::system::error_code& error);
	boost::asio::io_context& _ioc;
	short _port;
	boost::asio::ip::tcp::acceptor _acceptor;
	// 用来根据session_id管理所有的session
	std::map<std::string, std::shared_ptr<CSession>> _sessions;
	std::mutex _mutex;
};

