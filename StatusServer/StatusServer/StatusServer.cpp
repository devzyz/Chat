// StatusServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "ConfigMgr.h"
#include "StatusServiceImpl.h"
#include "const.h"
#include "LogMgr.h"

void RunServer() {
	auto& configMgr = ConfigMgr::GetInstance();

	std::string host = configMgr["StatusServer"]["Host"];
	std::string port = configMgr["StatusServer"]["Port"];
	std::string server_address = host + ":" + port;

	StatusServiceImpl service;

	grpc::ServerBuilder builder;
	// 监听端口和添加服务
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);

	// 构建并启动gRPC服务器
	std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
	SPDLOG_INFO("StatusServer listening on {}", server_address);

	// 下面的逻辑是用来优雅关闭的
	// io_context的目的是为了构造signal_set，signal_set将异步等待函数注册到io_context内
	// 然后通过一个独立的线程启动io_context,并阻塞主进程
	// 独立线程不断轮询，直到异步回调触发，然后会将主线程关闭
	// 然后主线程停止阻塞继续往下执行，然后将io_context停止

	// 创建Boost.Asio的io_context
	boost::asio::io_context io_context;
	// 创建singal_set用于捕获停止信号
	boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

	// 异步等待停止信号
	signals.async_wait([&server](const boost::system::error_code& error, int signal_number) {
		if (!error) {
			SPDLOG_INFO("StatusServer shutting down");
			server->Shutdown(); // 优雅地关闭服务器
		}
		});

	// 在单独的线程中运行io_context
	std::thread([&io_context]() {
		io_context.run();
		}).detach();

	// 等待服务器关闭
	server->Wait();
	io_context.stop();
}

int main()
{
	auto logger = LogMgr::GetInstance();
	if (!logger->InitLogMgr()) {
		return EXIT_FAILURE;
	}
	try {
		RunServer();
	}
	catch (std::exception& e) {
		SPDLOG_ERROR("StatusServer exception: {}", e.what());
		return EXIT_FAILURE;
	}

	return 0;
}
