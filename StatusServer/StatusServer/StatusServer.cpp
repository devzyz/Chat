// StatusServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "ConfigMgr.h"
#include "StatusGrpcServer.h"
#include "StatusRoutingProduction.h"
#include "const.h"
#include "LogMgr.h"
#include <boost/algorithm/string/trim.hpp>
#include <csignal>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

std::unique_ptr<StatusRouting> CreateConfiguredStatusRouting() {
	auto& config = ConfigMgr::GetInstance();
	std::stringstream names(config["ChatServers"]["Name"]);
	std::string section_name;
	std::vector<RoutingServer> servers;
	while (std::getline(names, section_name, ',')) {
		boost::algorithm::trim(section_name);
		auto section = config[section_name];
		servers.push_back({section["Name"], section["Host"], section["Port"]});
	}
	return CreateProductionStatusRouting(std::move(servers));
}

void RunServer() {
	auto& configMgr = ConfigMgr::GetInstance();

	std::string host = configMgr["StatusServer"]["Host"];
	std::string port = configMgr["StatusServer"]["Port"];
	std::string server_address = host + ":" + port;

	auto routing = CreateConfiguredStatusRouting();
	status::StatusGrpcServer server(*routing);
	if (!server.Start(server_address)) {
		throw std::runtime_error("failed to listen on gRPC address " + server_address);
	}
	SPDLOG_INFO("StatusServer listening on {}", server.BoundEndpoint());

	// 下面的逻辑是用来优雅关闭的
	// io_context的目的是为了构造signal_set，signal_set将异步等待函数注册到io_context内
	// 然后通过一个独立的线程启动io_context,并阻塞主进程
	// 独立线程不断轮询，直到异步回调触发，然后会将主线程关闭
	// 然后主线程停止阻塞继续往下执行，然后将io_context停止

	// 创建Boost.Asio的io_context
	boost::asio::io_context io_context;
	// 创建singal_set用于捕获停止信号
	boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
#ifdef _WIN32
	signals.add(SIGBREAK);
#endif

	// 异步等待停止信号
	signals.async_wait([&server](const boost::system::error_code& error, int signal_number) {
		if (!error) {
			SPDLOG_INFO("StatusServer shutting down");
			server.Stop(std::chrono::system_clock::now() + std::chrono::seconds(5));
		}
		});

	std::thread signal_thread;
	try {
		// 在单独的线程中运行io_context，并由 RunServer 明确 join。
		signal_thread = std::thread([&io_context]() {
			io_context.run();
			});

		server.Wait();
		io_context.stop();
		if (signal_thread.joinable()) {
			signal_thread.join();
		}
		SPDLOG_INFO("StatusServer stopped");
	}
	catch (...) {
		server.Stop(std::chrono::system_clock::now() + std::chrono::seconds(5));
		io_context.stop();
		if (signal_thread.joinable()) {
			signal_thread.join();
		}
		throw;
	}
}

int main(int argc, char* argv[])
{
	if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--config")) {
		std::cerr << "Usage: StatusServer.exe [--config <path>]" << std::endl;
		return EXIT_FAILURE;
	}
	if (argc == 3) {
		ConfigMgr::SetConfigPath(argv[2]);
	}
	try {
		ConfigMgr::GetInstance();
		auto logger = LogMgr::GetInstance();
		if (!logger->InitLogMgr()) {
			std::cerr << "StatusServer failed to initialize logging." << std::endl;
			return EXIT_FAILURE;
		}
		RunServer();
		logger->Close();
	}
	catch (const std::exception& e) {
		std::cerr << "StatusServer startup error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
