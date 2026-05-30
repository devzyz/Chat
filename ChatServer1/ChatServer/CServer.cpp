#include "CServer.h"
#include "AsioIOServicePool.h"
#include "CSession.h"
#include "UserMgr.h"

CServer::CServer(boost::asio::io_context& ioc, short port) : _ioc(ioc), _port(port),
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
	std::cout << "Server start success, on port : " << port << std::endl; // 日志todo...
	StartAcceptor(); // 开始监听
}

CServer::~CServer() {
	std::cout << "Server destruct listen on posrt : " << _port << std::endl; // 日志todo...
}

/**
 * @brief 
 * 用于异步接收连接
 * 
 */
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
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_sessions.insert(std::make_pair(new_session->GetSessionId(), new_session));
		}
		new_session->Start();
		
	}

	StartAcceptor();
}

/**
 * @brief 
 * @param session_id 
 * CServer内的某个CSession被移除了，代表这该服务器与tcp的连接关闭了，此时要将CServer中保存的CSession
 * 以及UserMgr中保存的CSession都清空
 */
void CServer::ClearSession(std::string session_id) {
	if (_sessions.find(session_id) != _sessions.end()) {
		// 移除UserMgr关联的CSession
		UserMgr::GetInstance()->RemoveUserSession(_sessions[session_id]->GetUserId());
	}
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_sessions.erase(session_id);
	}
}

