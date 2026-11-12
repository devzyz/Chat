#include "CServer.h"
#include "const.h"
#include "ConfigMgr.h"
#include "LogMgr.h"
#include "AsioIOServicePool.h"
#include "GateRequestProduction.h"
#include "LogicSystem.h"
#include <csignal>
#include <iostream>

/** @brief 解析启动配置并初始化本服务依赖，发布就绪信息后运行事件循环，按信号或错误执行关闭流程。 */
int main(int argc, char* argv[]) {
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--config")) {
        std::cerr << "Usage: GateServer.exe [--config <path>]" << std::endl;
        return EXIT_FAILURE;
    }
    if (argc == 3) {
        ConfigMgr::SetConfigPath(argv[2]);
    }

    try {
        ConfigMgr& config = ConfigMgr::GetInstance();
        auto logger = LogMgr::GetInstance();
        if (!logger->InitLogMgr()) {
            std::cerr << "GateServer failed to initialize logging." << std::endl;
            return EXIT_FAILURE;
        }

        const auto port = static_cast<unsigned short>(std::stoi(config["GateServer"]["Port"]));
        net::io_context ioc{1};
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
#ifdef _WIN32
        signals.add(SIGBREAK);
#endif
        auto gate_request = gate::CreateProductionGateRequest();
        auto logic = std::make_shared<LogicSystem>(*gate_request);
        const auto configured_host = config["GateServer"]["Host"];
        auto server =
            std::make_shared<CServer>(ioc, configured_host.empty() ? "0.0.0.0" : configured_host, port, logic);
        auto pool = AsioIOServicePool::GetInstance();

        // 在正常退出信号到来时停止监听、连接与工作池。
        signals.async_wait([&ioc, pool, server](const boost::system::error_code& err, int signal_number) {
            if (err) {
                return;
            }
            SPDLOG_INFO("GateServer shutting down, signal={}", signal_number);
            server->Stop();
            pool->Stop();
            ioc.stop();
        });

        try {
            server->Start();
            SPDLOG_INFO("GateServer listening on port={}", port);
            ioc.run();
        } catch (...) {
            server->Stop();
            pool->Stop();
            throw;
        }
        server->Stop();
        pool->Stop();
        SPDLOG_INFO("GateServer stopped");
        logger->Close();
    } catch (const std::exception& e) {
        std::cerr << "GateServer startup error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
