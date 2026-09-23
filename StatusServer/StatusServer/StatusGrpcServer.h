#pragma once

#include "StatusServiceImpl.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace status {

/** @brief 拥有 gRPC 服务器及状态路由服务，提供显式启动、停服和等待边界。 */
class StatusGrpcServer final {
public:
	/** @brief 初始化StatusGrpcServer，拥有 gRPC 服务器及状态路由服务，提供显式启动、停服和等待边界。 */
	explicit StatusGrpcServer(StatusRouting& routing);
	/** @brief 执行服务关闭流程并释放本对象拥有的运行资源。 */
	~StatusGrpcServer();

	/** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
	StatusGrpcServer(const StatusGrpcServer&) = delete;
	/** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
	StatusGrpcServer& operator=(const StatusGrpcServer&) = delete;

	/** @brief 创建并绑定 gRPC 服务，保存实际端点；已启动或正在停止、绑定失败时返回 false。 */
	bool Start(const std::string& endpoint);
	/** @brief 返回实际绑定的完整服务端点文本。 */
	std::string BoundEndpoint() const;
	/** @brief 返回实际绑定的监听地址。 */
	std::string BoundAddress() const;
	/** @brief 返回实际绑定的监听端口，便于动态端口启动验证。 */
	std::uint16_t BoundPort() const noexcept;
	/** @brief 查询 gRPC 服务器是否已成功创建且处于可服务状态。 */
	bool Ready() const noexcept;
	/** @brief 阻塞等待 gRPC 服务器关闭，须配合其他线程执行 Stop。 */
	void Wait();
	/**
	 * @brief 以指定期限 Shutdown 并阻塞 Wait，完成后销毁服务器。
	 * 已停止返回 true；未启动、正在停止或关闭抛异常返回 false。期限控制 RPC 取消，不代替 Wait 完成。
	 */
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
