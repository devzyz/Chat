#pragma once

#include "StatusServiceImpl.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace status {

class StatusGrpcServer final {
public:
	explicit StatusGrpcServer(StatusRouting& routing);
	~StatusGrpcServer();

	StatusGrpcServer(const StatusGrpcServer&) = delete;
	StatusGrpcServer& operator=(const StatusGrpcServer&) = delete;

	bool Start(const std::string& endpoint);
	std::string BoundEndpoint() const;
	std::string BoundAddress() const;
	std::uint16_t BoundPort() const noexcept;
	bool Ready() const noexcept;
	void Wait();
	bool Stop(std::chrono::system_clock::time_point deadline) noexcept;

private:
	StatusServiceImpl service_;
	mutable std::mutex mutex_;
	std::unique_ptr<grpc::Server> server_;
	std::string bound_address_;
	std::uint16_t bound_port_ = 0;
	bool stopping_ = false;
	bool stopped_ = false;
};

} // namespace status
