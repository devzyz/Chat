#include "CServer.h"
#include "AsioIOServicePool.h"
#include "CSession.h"
#include "UserMgr.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"

CServer::CServer(boost::asio::io_context& ioc, short port) : _ioc(ioc), _port(port),
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)), _timer(ioc, std::chrono::seconds(60)) {
	SPDLOG_INFO("TCP server started, port={}", port); // 日志todo...
	
}

// 后置初始化，保证shared_from_this已经存在
void CServer::init() {
	// 等待60秒后触发Lambda回调
	_timer.async_wait([self = shared_from_this()](const boost::system::error_code& e) {
		self->on_timer(e);
		});

	StartAcceptor(); // 开始监听
}

CServer::~CServer() {
	SPDLOG_INFO("TCP server destructed, port={}", _port);
}

void CServer::stop() {
	_sessions.clear();
	_timer.cancel();
}

/**
 * @brief 
 * 用于异步接收连接
 * 
 */
void CServer::StartAcceptor() {
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, shared_from_this());
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
	std::lock_guard<std::mutex> lock(_mutex);
	if (_sessions.find(session_id) != _sessions.end()) {
		auto uid = _sessions[session_id]->GetUserId();
		// 移除UserMgr关联的CSession
		// UserMgr中可能已经被其他进程更新为最新的session连接了，因此可能内部实际不需要删除
		UserMgr::GetInstance()->RemoveUserSession(uid, session_id);
	}
	
	// 从本地的sessions中清除与客户端的连接
	_sessions.erase(session_id);
}

// 检查当前的session_id是能够正常使用
bool CServer::CheckSessionValid(std::string session_id) {
	std::lock_guard<std::mutex> lock(_mutex);
	auto it = _sessions.find(session_id);
	if (it != _sessions.end()) {
		return true;
	}
	return false;
}

// 定时器，触发对当前session连接的检测
void CServer::on_timer(const boost::system::error_code& e) {
	if (e) {
		SPDLOG_WARN("server timer error: {}", e.message());
		return;
	}
	// 暂存已过期的session
	std::vector<std::shared_ptr<CSession>> _expired_sessions;
	int session_count = 0; // 计算还存活的session

	// 因为这里的思路就是遍历一下所有的session，查看一下是否超时
	// 因此我们可以通过先加锁，然后将_sessions拷贝一份，然后通过对副本来进行处理
	// 这样能够提高锁的精度
	// 同时可以保证访问的CSession一定是有效的
	std::map<std::string, std::shared_ptr<CSession>> _sessions_copy;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_sessions_copy = _sessions;
	}

	std::time_t now = time(nullptr);
	for (auto it = _sessions_copy.begin(); it != _sessions_copy.end(); it++) {
		// 检查session心跳是否超时
		auto b_expired = it->second->CheckHeartBeatAccurate(now);
		if (b_expired) {
			// 如果超时，则关闭连接，_socket关闭了，则会触发async_read的错误事件
			it->second->Close();

			_expired_sessions.push_back(it->second);
		}
		else {
			session_count++;
		}
	}

	// 此时session_count记录了当前server还连接的session数量
	// 可以直接将其更新到redis中，作为负载均衡的参考
	// 为什么这里不需要加锁，首先这是定时操作，同一个进程中只会触发一次
	// 而多进程操作的又不是同一个变量，同一个服务器只会读取和修改本服务器的count数量
	auto& configMgr = ConfigMgr::GetInstance();
	auto self_server_name = configMgr["SelfServer"]["Name"];
	auto count_str = std::to_string(session_count);
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, self_server_name, count_str);

	// 处理过期的session
	// 删除redis中的相应的信息
	for (auto& session : _expired_sessions) {
		session->DealExceptionSession();
	}

	// 设置下一个60秒的检测
	_timer.expires_after(std::chrono::seconds(60));
	_timer.async_wait([self = shared_from_this()](const boost::system::error_code& e) {
		self->on_timer(e);
		});
}
