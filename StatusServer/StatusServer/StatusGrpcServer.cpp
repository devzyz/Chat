#include "StatusGrpcServer.h"

#include <limits>
#include <utility>

namespace status {

StatusGrpcServer::StatusGrpcServer(StatusRouting& routing)
	: service_(routing) {
}

StatusGrpcServer::~StatusGrpcServer() {
	Stop(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

bool StatusGrpcServer::Start(const std::string& endpoint) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (server_ || stopping_ || endpoint.empty()) {
		return false;
	}

	const auto separator = endpoint.rfind(':');
	if (separator == std::string::npos || separator == 0 || separator + 1 >= endpoint.size()) {
		return false;
	}
	const auto address = endpoint.substr(0, separator);

	grpc::ServerBuilder builder;
	int selected_port = 0;
	builder.AddListeningPort(endpoint, grpc::InsecureServerCredentials(), &selected_port);
	builder.RegisterService(&service_);
	auto server = builder.BuildAndStart();
	if (!server || selected_port <= 0 || selected_port > (std::numeric_limits<std::uint16_t>::max)()) {
		return false;
	}

	bound_address_ = address;
	bound_port_ = static_cast<std::uint16_t>(selected_port);
	stopped_ = false;
	server_ = std::move(server);
	return true;
}

std::string StatusGrpcServer::BoundEndpoint() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return bound_port_ == 0 ? std::string{} : bound_address_ + ":" + std::to_string(bound_port_);
}

std::string StatusGrpcServer::BoundAddress() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return bound_address_;
}

std::uint16_t StatusGrpcServer::BoundPort() const noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	return bound_port_;
}

bool StatusGrpcServer::Ready() const noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	return server_ != nullptr && !stopping_ && !stopped_;
}

void StatusGrpcServer::Wait() {
	grpc::Server* server = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		server = server_.get();
	}
	if (server) {
		server->Wait();
	}
}

bool StatusGrpcServer::Stop(std::chrono::system_clock::time_point deadline) noexcept {
	grpc::Server* server = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_) {
			return true;
		}
		if (!server_ || stopping_) {
			return false;
		}
		stopping_ = true;
		server = server_.get();
	}

	try {
		server->Shutdown(deadline);
		server->Wait();
	}
	catch (...) {
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = false;
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	server_.reset();
	bound_port_ = 0;
	stopping_ = false;
	stopped_ = true;
	return true;
}

} // namespace status
