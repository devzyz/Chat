#include "CServer.h"
#include "HttpConnection.h"
#include "AsioIOServicePool.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

CServer::CServer(
	boost::asio::io_context& ioc,
	unsigned short port,
	std::shared_ptr<LogicSystem> logic)
	: CServer(ioc, "0.0.0.0", port, std::move(logic)) {
}

CServer::CServer(
	boost::asio::io_context& ioc,
	const std::string& address,
	unsigned short port,
	std::shared_ptr<LogicSystem> logic)
	: _acceptor(ioc), _ioc(ioc), _logic(std::move(logic)) {
	if (!_logic) {
		throw std::invalid_argument("Gate HTTP transport requires GateRequest routing");
	}
	const auto endpoint = tcp::endpoint(boost::asio::ip::make_address(address), port);
	_acceptor.open(endpoint.protocol());
	_acceptor.set_option(tcp::acceptor::reuse_address(true));
	_acceptor.bind(endpoint);
	_acceptor.listen(net::socket_base::max_listen_connections);
	const auto bound = _acceptor.local_endpoint();
	_bound_address = bound.address().to_string();
	_bound_port = bound.port();
}

void CServer::Start() {
	bool expected = false;
	if (_stopping.load() || !_started.compare_exchange_strong(expected, true)) {
		return;
	}
	AcceptNext();
}

void CServer::AcceptNext() {
	if (_stopping.load()) {
		return;
	}
	auto self = shared_from_this();
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context, _logic);
	{
		std::lock_guard<std::mutex> lock(connections_mutex_);
		connections_.erase(
			std::remove_if(connections_.begin(), connections_.end(),
				[](const auto& connection) { return connection.expired(); }),
			connections_.end());
		connections_.push_back(new_con);
	}
	_acceptor.async_accept(new_con->GetSocket(), [self, new_con](beast::error_code ec) {
		try {
			// 出错放弃这个链接，继续监听其他链接
			if (ec) {
				if (!self->_stopping.load()) {
					self->AcceptNext();
				}
				return;
			}

			// 创建新连接，并且创建HttpConnection类管理这个链接
			new_con->Start();

			// 继续监听
			self->AcceptNext();
		}
		catch (std::exception& e) {
			SPDLOG_ERROR("accept connection exception: {}", e.what());
		}
		});
}

void CServer::Stop() {
	bool expected = false;
	if (!_stopping.compare_exchange_strong(expected, true)) {
		return;
	}
	beast::error_code error;
	_acceptor.cancel(error);
	_acceptor.close(error);
	std::vector<std::shared_ptr<HttpConnection>> connections;
	{
		std::lock_guard<std::mutex> lock(connections_mutex_);
		for (const auto& weak_connection : connections_) {
			if (auto connection = weak_connection.lock()) {
				connections.push_back(std::move(connection));
			}
		}
		connections_.clear();
	}
	for (const auto& connection : connections) {
		connection->Stop();
	}
}

std::string CServer::BoundAddress() const {
	return _bound_address;
}

unsigned short CServer::BoundPort() const noexcept {
	return _bound_port;
}
