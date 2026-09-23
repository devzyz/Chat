#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class SessionLifecycleCoordinator;
class LogicDispatcher;
class StatusRouting;

namespace gate {
class GateRequest;
}

namespace integration {

using Deadline = std::chrono::steady_clock::time_point;

/** 保存传输宿主实际绑定的数值回环地址和端口。 */
struct LoopbackEndpoint {
	std::string address;
	std::uint16_t port = 0;
};

/** 保存宿主清理完成状态及诊断。 */
struct CleanupResult {
	bool complete = false;
	std::string detail;
};

enum class HostFamily {
	GateHttp,
	StatusGrpc,
	ChatTcp,
};

/** 线程安全地发布并查询第一次宿主清理结果。 */
class CleanupObserver {
public:
	/** 在锁内仅发布首次清理结果。 */
	void Publish(CleanupResult result);
	/** 查询是否已有清理结果发布。 */
	bool Published() const;
	/** 返回当前清理结果的副本。 */
	CleanupResult Result() const;

private:
	mutable std::mutex mutex_;
	bool published_ = false;
	CleanupResult result_;
};

/** 独占持有宿主需要的生产业务模块，允许移动但不允许共享复制。 */
struct ProductionModules {
	/** 初始化为空的生产模块集合。 */
	ProductionModules();
	/** 销毁所有独占模块，传输宿主须先结束使用。 */
	~ProductionModules();
	/** 转移各生产模块的独占所有权。 */
	ProductionModules(ProductionModules&&) noexcept;
	/** 移动替换生产模块集合。 */
	ProductionModules& operator=(ProductionModules&&) noexcept;

	/** 禁止复制独占业务模块。 */
	ProductionModules(const ProductionModules&) = delete;
	/** 禁止复制赋值独占业务模块。 */
	ProductionModules& operator=(const ProductionModules&) = delete;

	std::unique_ptr<LogicDispatcher> logic_dispatcher;
	std::unique_ptr<SessionLifecycleCoordinator> chat_sessions;
	std::unique_ptr<gate::GateRequest> gate_request;
	std::unique_ptr<StatusRouting> status_routing;
};

/** 定义集成传输宿主的绑定、真实协议就绪及有界停机接口。 */
class TransportHost {
public:
	/** 经接口销毁具体传输宿主。 */
	virtual ~TransportHost() = default;
	/** 返回实际绑定端点，不能以请求端口代替绑定结果。 */
	virtual LoopbackEndpoint BoundEndpoint() const = 0;
	/** 在给定期限内验证协议就绪，失败或未就绪返回假。 */
	virtual bool WaitReady(Deadline deadline) = 0;
	/** 在给定期限内停止监听和所属连接并返回清理证据。 */
	virtual CleanupResult Stop(Deadline deadline) = 0;
};

using ModuleFactory = std::function<ProductionModules()>;
using TransportFactory = std::function<std::unique_ptr<TransportHost>(
	const LoopbackEndpoint& requested,
	const ProductionModules& modules)>;

/** 声明宿主族、回环端点、有限期限、模块与传输工厂及清理观察者。 */
struct HostSpec {
	HostFamily family = HostFamily::GateHttp;
	LoopbackEndpoint endpoint;
	Deadline deadline;
	ModuleFactory create_modules;
	TransportFactory create_transport;
	std::shared_ptr<CleanupObserver> cleanup;
};

/** 拥有生产模块和传输宿主，串行协调就绪与幂等停机并发布清理结果。 */
class HostHandle {
public:
	/** 尚未停止时按所属期限执行停机。 */
	~HostHandle();
	/** 禁止移动以保持传输对模块的借用稳定。 */
	HostHandle(HostHandle&&) = delete;
	/** 禁止移动赋值以保持传输对模块的借用稳定。 */
	HostHandle& operator=(HostHandle&&) = delete;

	/** 在锁内查询实际绑定端点。 */
	LoopbackEndpoint BoundEndpoint() const;
	/** 仅对未停止且期限仍有效的宿主执行就绪检查，异常按失败返回。 */
	bool Ready();
	/** 一次性停止传输并发布结果；无界期限或停机异常记为失败。 */
	CleanupResult Stop(Deadline deadline);

private:
	friend class IntegrationHostFactory;
	/** 接管已就绪模块与传输，保存期限和清理观察者。 */
	HostHandle(
		ProductionModules modules,
		std::unique_ptr<TransportHost> transport,
		Deadline deadline,
		std::shared_ptr<CleanupObserver> cleanup);

	ProductionModules modules_;
	std::unique_ptr<TransportHost> transport_;
	Deadline deadline_;
	std::shared_ptr<CleanupObserver> cleanup_;
	mutable std::mutex mutex_;
	bool stopped_ = false;
	CleanupResult stop_result_;
};

/** 校验测试宿主组合并保证失败启动可观察地清理。 */
class IntegrationHostFactory {
public:
	/** 要求数值回环、有限未来期限和完整生产模块，绑定及协议就绪失败时清理并抛异常。 */
	static std::unique_ptr<HostHandle> Start(HostSpec spec);
};

} // namespace integration
