#pragma once

#include "ChatSessionState.h"

#include <boost/asio.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CSession;
class LogicDispatcher;

namespace chat_transport {

class CServer : public std::enable_shared_from_this<CServer>
{
public:
	using SessionCountObserver = std::function<void(std::size_t)>;

	CServer(
		boost::asio::io_context& ioc,
		std::string address,
		std::uint16_t port,
		std::shared_ptr<ChatSessionState> session_state,
		std::shared_ptr<LogicDispatcher> dispatcher,
		SessionCountObserver session_count_observer = {});
	~CServer();

	bool Start();
	void Stop();
	bool Ready() const noexcept;
	const std::string& BoundAddress() const noexcept;
	std::uint16_t BoundPort() const noexcept;

	// Compatibility names retained for existing formal callers.
	void init();
	void stop();

	void ClearSession(const ChatSessionState::Handle& session);
	bool CheckSessionValid(const ChatSessionState::Handle& session);
	void on_timer(const boost::system::error_code& error);

private:
	void StartAcceptor();
	void HandleAcceptor(
		std::shared_ptr<CSession> new_session,
		const boost::system::error_code& error);

	boost::asio::io_context& _ioc;
	std::string _address;
	std::uint16_t _port;
	boost::asio::ip::tcp::acceptor _acceptor;
	std::vector<std::pair<ChatSessionState::Handle, std::shared_ptr<CSession>>> _sessions;
	std::shared_ptr<ChatSessionState> _session_state;
	std::shared_ptr<LogicDispatcher> _dispatcher;
	SessionCountObserver _session_count_observer;
	mutable std::mutex _mutex;
	boost::asio::steady_timer _timer;
	std::atomic<bool> _started{false};
	std::atomic<bool> _stopping{false};
};

} // namespace chat_transport
