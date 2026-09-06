#include "CServer.h"

#include "CSession.h"
#include "LogicDispatcher.h"
#include "LogMgr.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace chat_transport {

CServer::CServer(
	boost::asio::io_context& ioc,
	std::string address,
	std::uint16_t port,
	std::shared_ptr<ChatSessionState> session_state,
	std::shared_ptr<LogicDispatcher> dispatcher,
	SessionCountObserver session_count_observer)
	: _ioc(ioc),
	  _address(std::move(address)),
	  _port(port),
	  _acceptor(ioc),
	  _session_state(std::move(session_state)),
	  _dispatcher(std::move(dispatcher)),
	  _session_count_observer(std::move(session_count_observer)),
	  _timer(ioc) {
	if (!_session_state || !_dispatcher) {
		throw std::invalid_argument("Chat transport dependencies must not be null");
	}
	const auto bind_address = boost::asio::ip::make_address(_address);
	if (!bind_address.is_loopback()) {
		throw std::invalid_argument("Chat transport address must be numeric loopback");
	}
	boost::asio::ip::tcp::endpoint endpoint(bind_address, _port);
	_acceptor.open(endpoint.protocol());
	_acceptor.set_option(boost::asio::socket_base::reuse_address(false));
	_acceptor.bind(endpoint);
	_acceptor.listen();
	_port = _acceptor.local_endpoint().port();
	SPDLOG_INFO("TCP server bound, address={}, port={}", _address, _port);
}

CServer::~CServer() {
	Stop();
	SPDLOG_INFO("TCP server destructed, port={}", _port);
}

bool CServer::Start() {
	bool expected = false;
	if (!_started.compare_exchange_strong(expected, true)) {
		return !_stopping.load();
	}
	if (_stopping.load() || !_acceptor.is_open()) {
		return false;
	}
	StartAcceptor();
	if (_session_count_observer) {
		_timer.expires_after(std::chrono::seconds(60));
		_timer.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
			self->on_timer(error);
		});
	}
	return true;
}

void CServer::init() {
	(void)Start();
}

void CServer::Stop() {
	bool expected = false;
	if (!_stopping.compare_exchange_strong(expected, true)) {
		return;
	}
	boost::system::error_code ignored;
	_timer.cancel();
	_acceptor.cancel(ignored);
	_acceptor.close(ignored);

	std::vector<std::shared_ptr<CSession>> sessions;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (const auto& entry : _sessions) {
			sessions.push_back(entry.second);
		}
		_sessions.clear();
	}
	for (const auto& session : sessions) {
		session->Close();
	}
}

void CServer::stop() {
	Stop();
}

bool CServer::Ready() const noexcept {
	return _started.load() && !_stopping.load() && _acceptor.is_open();
}

bool CServer::Stopped() const noexcept {
	return _stopping.load() && !_acceptor.is_open();
}

const std::string& CServer::BoundAddress() const noexcept {
	return _address;
}

std::uint16_t CServer::BoundPort() const noexcept {
	return _port;
}

void CServer::StartAcceptor() {
	if (_stopping.load() || !_acceptor.is_open()) {
		return;
	}
	auto new_session = std::make_shared<CSession>(
		_ioc, shared_from_this(), _session_state, _dispatcher);
	_acceptor.async_accept(new_session->GetSocket(),
		[self = shared_from_this(), new_session](const boost::system::error_code& error) {
			self->HandleAcceptor(new_session, error);
		});
}

void CServer::HandleAcceptor(
	std::shared_ptr<CSession> new_session,
	const boost::system::error_code& error) {
	if (!error && !_stopping.load()) {
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_sessions.emplace_back(new_session->GetHandle(), new_session);
		}
		new_session->Start();
	}
	if (!_stopping.load()) {
		StartAcceptor();
	}
}

void CServer::ClearSession(const ChatSessionState::Handle& session) {
	_session_state->Close(session);
	std::lock_guard<std::mutex> lock(_mutex);
	_sessions.erase(std::remove_if(_sessions.begin(), _sessions.end(),
		[&session](const auto& entry) { return entry.first == session; }), _sessions.end());
}

bool CServer::CheckSessionValid(const ChatSessionState::Handle& session) {
	std::lock_guard<std::mutex> lock(_mutex);
	return std::any_of(_sessions.begin(), _sessions.end(),
		[&session](const auto& entry) { return entry.first == session; });
}

void CServer::on_timer(const boost::system::error_code& error) {
	if (error || _stopping.load() || !_session_count_observer) {
		return;
	}
	std::vector<std::shared_ptr<CSession>> sessions;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (const auto& entry : _sessions) {
			sessions.push_back(entry.second);
		}
	}

	std::size_t active = 0;
	std::time_t now = time(nullptr);
	for (const auto& session : sessions) {
		if (session->CheckHeartBeatAccurate(now)) {
			session->DealExceptionSession();
		} else {
			++active;
		}
	}
	_session_count_observer(active);
	_timer.expires_after(std::chrono::seconds(60));
	_timer.async_wait([self = shared_from_this()](const boost::system::error_code& next_error) {
		self->on_timer(next_error);
	});
}

} // namespace chat_transport
