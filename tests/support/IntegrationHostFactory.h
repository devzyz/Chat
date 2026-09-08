#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class ChatSessionState;
class LogicDispatcher;
class StatusRouting;

namespace gate {
class GateRequest;
}

namespace integration {

using Deadline = std::chrono::steady_clock::time_point;

struct LoopbackEndpoint {
	std::string address;
	std::uint16_t port = 0;
};

struct CleanupResult {
	bool complete = false;
	std::string detail;
};

enum class HostFamily {
	GateHttp,
	StatusGrpc,
	ChatTcp,
};

class CleanupObserver {
public:
	void Publish(CleanupResult result);
	bool Published() const;
	CleanupResult Result() const;

private:
	mutable std::mutex mutex_;
	bool published_ = false;
	CleanupResult result_;
};

struct ProductionModules {
	ProductionModules();
	~ProductionModules();
	ProductionModules(ProductionModules&&) noexcept;
	ProductionModules& operator=(ProductionModules&&) noexcept;

	ProductionModules(const ProductionModules&) = delete;
	ProductionModules& operator=(const ProductionModules&) = delete;

	std::unique_ptr<LogicDispatcher> logic_dispatcher;
	std::unique_ptr<ChatSessionState> chat_sessions;
	std::unique_ptr<gate::GateRequest> gate_request;
	std::unique_ptr<StatusRouting> status_routing;
};

class TransportHost {
public:
	virtual ~TransportHost() = default;
	virtual LoopbackEndpoint BoundEndpoint() const = 0;
	virtual bool WaitReady(Deadline deadline) = 0;
	virtual CleanupResult Stop(Deadline deadline) = 0;
};

using ModuleFactory = std::function<ProductionModules()>;
using TransportFactory = std::function<std::unique_ptr<TransportHost>(
	const LoopbackEndpoint& requested,
	const ProductionModules& modules)>;

struct HostSpec {
	HostFamily family = HostFamily::GateHttp;
	LoopbackEndpoint endpoint;
	Deadline deadline;
	ModuleFactory create_modules;
	TransportFactory create_transport;
	std::shared_ptr<CleanupObserver> cleanup;
};

class HostHandle {
public:
	~HostHandle();
	HostHandle(HostHandle&&) = delete;
	HostHandle& operator=(HostHandle&&) = delete;

	LoopbackEndpoint BoundEndpoint() const;
	bool Ready();
	CleanupResult Stop(Deadline deadline);

private:
	friend class IntegrationHostFactory;
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

class IntegrationHostFactory {
public:
	static std::unique_ptr<HostHandle> Start(HostSpec spec);
};

} // namespace integration
