#pragma once
#include "const.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

class HttpConnection;
class LogicSystem;

/** @brief 拥有 HTTP 监听器并串行接受连接，将请求交给共享业务分发器。 */
class CServer : public std::enable_shared_from_this<CServer>
{
public:
	/** @brief 返回 HTTP 请求正文允许的最大字节数。 */
	static constexpr std::size_t MaxRequestBodyBytes() noexcept { return 8192; }

	/** @brief 在 IPv4 通配地址绑定端口并建立监听，借用事件循环并持有业务分发器；绑定失败抛异常。 */
	CServer(
		boost::asio::io_context& ioc,
		unsigned short port,
		std::shared_ptr<LogicSystem> logic);
	/** @brief 在显式数字地址绑定端口并建立监听；端口可为零以动态分配，绑定失败抛异常。 */
	CServer(
		boost::asio::io_context& ioc,
		const std::string& address,
		unsigned short port,
		std::shared_ptr<LogicSystem> logic);
	/** @brief 幂等地在已绑定监听器上安排首次异步接收，已停止时不重启。 */
	void Start();
	/** @brief 停止接收新工作并关闭当前服务的监听或执行器；后续销毁由所属生命周期流程负责。 */
	void Stop();
	/** @brief 返回实际绑定的监听地址。 */
	std::string BoundAddress() const;
	/** @brief 返回实际绑定的监听端口，便于动态端口启动验证。 */
	unsigned short BoundPort() const noexcept;

private:
	/** @brief 在监听器运行期间安排下一次异步 accept，成功后启动独立 HTTP 连接。 */
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
