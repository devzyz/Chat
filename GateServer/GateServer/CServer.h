#pragma once
#include "const.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

class HttpConnection;
class LogicSystem;

class CServer : public std::enable_shared_from_this<CServer>
{
public:
	static constexpr std::size_t MaxRequestBodyBytes() noexcept { return 8192; }

	CServer(
		boost::asio::io_context& ioc,
		unsigned short port,
		std::shared_ptr<LogicSystem> logic);
	CServer(
		boost::asio::io_context& ioc,
		const std::string& address,
		unsigned short port,
		std::shared_ptr<LogicSystem> logic);
	void Start();
	void Stop();
	std::string BoundAddress() const;
	unsigned short BoundPort() const noexcept;

private:
	void AcceptNext();

	tcp::acceptor _acceptor;
	net::io_context& _ioc;
	std::shared_ptr<LogicSystem> _logic;
	std::string _bound_address;
	unsigned short _bound_port = 0;
	std::atomic<bool> _started{false};
	std::atomic<bool> _stopping{false};
	std::mutex connections_mutex_;
	std::vector<std::weak_ptr<HttpConnection>> connections_;
};
