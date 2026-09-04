#include "IntegrationHostFactory.h"

#include "ChatSessionState.h"
#include "GateRequest.h"
#include "LogicDispatcher.h"
#include "StatusRouting.h"

#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace integration {
namespace {

bool IsIpv4Loopback(std::string_view address) {
	std::array<unsigned int, 4> octets{};
	for (std::size_t index = 0; index < octets.size(); ++index) {
		const auto separator = address.find('.');
		const auto component = address.substr(0, separator);
		if (component.empty()) {
			return false;
		}

		unsigned int value = 0;
		const auto parsed = std::from_chars(
			component.data(), component.data() + component.size(), value);
		if (parsed.ec != std::errc{} || parsed.ptr != component.data() + component.size() || value > 255) {
			return false;
		}
		octets[index] = value;

		if (index + 1 == octets.size()) {
			if (separator != std::string_view::npos) {
				return false;
			}
		} else {
			if (separator == std::string_view::npos) {
				return false;
			}
			address.remove_prefix(separator + 1);
		}
	}
	return octets[0] == 127;
}

bool IsNumericLoopback(const std::string& address) {
	return address == "::1" || IsIpv4Loopback(address);
}

bool IsBoundedDeadline(Deadline deadline) {
	return deadline != Deadline{} && deadline != Deadline::max();
}

bool IsFutureOwnedDeadline(Deadline deadline) {
	return IsBoundedDeadline(deadline) && deadline > std::chrono::steady_clock::now();
}

bool HasRequiredModules(HostFamily family, const ProductionModules& modules) {
	switch (family) {
	case HostFamily::GateHttp:
		return modules.gate_request != nullptr;
	case HostFamily::StatusGrpc:
		return modules.status_routing != nullptr;
	case HostFamily::ChatTcp:
		return modules.logic_dispatcher != nullptr && modules.chat_sessions != nullptr;
	}
	return false;
}

CleanupResult StopForFailedStart(
	TransportHost& transport,
	Deadline deadline,
	const std::shared_ptr<CleanupObserver>& cleanup) noexcept {
	CleanupResult result;
	try {
		result = transport.Stop(deadline);
	} catch (...) {
		result = {false, "transport stop threw during failed start"};
	}
	cleanup->Publish(result);
	return result;
}

} // namespace

void CleanupObserver::Publish(CleanupResult result) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!published_) {
		result_ = std::move(result);
		published_ = true;
	}
}

bool CleanupObserver::Published() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return published_;
}

CleanupResult CleanupObserver::Result() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return result_;
}

ProductionModules::ProductionModules() = default;
ProductionModules::~ProductionModules() = default;
ProductionModules::ProductionModules(ProductionModules&&) noexcept = default;
ProductionModules& ProductionModules::operator=(ProductionModules&&) noexcept = default;

HostHandle::HostHandle(
	ProductionModules modules,
	std::unique_ptr<TransportHost> transport,
	Deadline deadline,
	std::shared_ptr<CleanupObserver> cleanup)
	: modules_(std::move(modules)),
	  transport_(std::move(transport)),
	  deadline_(deadline),
	  cleanup_(std::move(cleanup)) {
}

HostHandle::~HostHandle() {
	if (!stopped_) {
		Stop(deadline_);
	}
}

LoopbackEndpoint HostHandle::BoundEndpoint() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return transport_->BoundEndpoint();
}

bool HostHandle::Ready() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (stopped_ || !IsFutureOwnedDeadline(deadline_)) {
		return false;
	}
	try {
		return transport_->WaitReady(deadline_);
	} catch (...) {
		return false;
	}
}

CleanupResult HostHandle::Stop(Deadline deadline) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (stopped_) {
		return stop_result_;
	}

	stopped_ = true;
	if (!IsBoundedDeadline(deadline)) {
		stop_result_ = {false, "stop deadline is missing or unbounded"};
	} else {
		try {
			stop_result_ = transport_->Stop(deadline);
		} catch (...) {
			stop_result_ = {false, "transport stop threw"};
		}
	}
	cleanup_->Publish(stop_result_);
	return stop_result_;
}

std::unique_ptr<HostHandle> IntegrationHostFactory::Start(HostSpec spec) {
	if (!IsNumericLoopback(spec.endpoint.address)) {
		throw std::invalid_argument("integration host endpoint must be numeric loopback");
	}
	if (!IsFutureOwnedDeadline(spec.deadline)) {
		throw std::invalid_argument("integration host deadline is missing, expired, or unbounded");
	}
	if (!spec.create_modules || !spec.create_transport || !spec.cleanup) {
		throw std::invalid_argument("integration host factories and cleanup observer are required");
	}

	auto modules = spec.create_modules();
	if (!HasRequiredModules(spec.family, modules)) {
		throw std::invalid_argument("integration host requires its concrete Phase 3A production modules");
	}
	auto transport = spec.create_transport(spec.endpoint, modules);
	if (!transport) {
		throw std::invalid_argument("integration transport factory returned no host");
	}

	try {
		const auto bound = transport->BoundEndpoint();
		if (!IsNumericLoopback(bound.address) || bound.port == 0) {
			StopForFailedStart(*transport, spec.deadline, spec.cleanup);
			throw std::runtime_error("integration transport did not publish a bound loopback endpoint");
		}
		if (!transport->WaitReady(spec.deadline)) {
			StopForFailedStart(*transport, spec.deadline, spec.cleanup);
			throw std::runtime_error("integration transport did not become protocol-ready before its deadline");
		}
	} catch (...) {
		if (!spec.cleanup->Published()) {
			StopForFailedStart(*transport, spec.deadline, spec.cleanup);
		}
		throw;
	}

	return std::unique_ptr<HostHandle>(new HostHandle(
		std::move(modules), std::move(transport), spec.deadline, std::move(spec.cleanup)));
}

} // namespace integration
