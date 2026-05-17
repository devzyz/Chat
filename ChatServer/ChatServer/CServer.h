#pragma once
#include <boost/asio.hpp>
#include <map>
#include <mutex>

class CSession;
class CServer : public std::enable_shared_from_this<CServer>
{
public:
	CServer(boost::asio::io_context& ioc, short port);
	~CServer();
	void ClearSession(std::string uuid);
private:
	void StartAcceptor();
	void HandleAcceptor(std::shared_ptr<CSession> new_session, const boost::system::error_code& error);
	boost::asio::io_context& _ioc;
	short _port;
	boost::asio::ip::tcp::acceptor _acceptor;
	std::map<std::string, std::shared_ptr<CSession>> _sessions;
	std::mutex _mutex;
};

