#include "CServer.h"
#include "AsioIOServicePool.h"
#include "CSession.h"

CServer::CServer(boost::asio::io_context& ioc, short port) : _ioc(ioc), _port(port),
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
	std::cout << "Server start success, on port : " << port << std::endl;
	StartAcceptor();
}

CServer::~CServer() {
	std::cout << "Server destruct listen on posrt : " << _port << std::endl;
}

void CServer::StartAcceptor() {
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(), 
		std::bind(&CServer::HandleAcceptor, this, new_session, std::placeholders::_1));
}

/**
 * @brief 
 * @param new_session 
 * @param error 
 * 用于处理连接的回调
 */
void CServer::HandleAcceptor(std::shared_ptr<CSession> new_session, const boost::system::error_code& error) {
	if (!error) {
		new_session->Start();
		std::lock_guard<std::mutex> lock(_mutex);
		_sessions.insert(std::make_pair(new_session->GetUuid(), new_session));
	}

	StartAcceptor();
}

void CServer::ClearSession(std::string uuid) {
	std::lock_guard<std::mutex> lock(_mutex);
	_sessions.erase(uuid);
}

