#include "CServer.h"
#include "const.h"
#include "ConfigMgr.h"
#include <hiredis/hiredis.h>
#include "RedisMgr.h"
#include "LogMgr.h"
#include "AsioIOServicePool.h"
#include "GateRequestProduction.h"
#include "LogicSystem.h"
#include <csignal>
#include <iostream>

void TestRedisMgr() {
    assert(RedisMgr::GetInstance()->Set("blogwebsite", "llfc.club"));
    std::string value = "";
    assert(RedisMgr::GetInstance()->Get("blogwebsite", value));
    assert((RedisMgr::GetInstance()->Get("nonekey", value)) == false);
    assert(RedisMgr::GetInstance()->HSet("bloginfo", "blogwebsite", "llfc.club"));
    assert(RedisMgr::GetInstance()->HGet("bloginfo", "blogwebsite", value));
    assert(RedisMgr::GetInstance()->ExistsKey("bloginfo"));
    assert(RedisMgr::GetInstance()->Del("bloginfo"));
    assert(RedisMgr::GetInstance()->Del("bloginfo"));
    assert(RedisMgr::GetInstance()->ExistsKey("bloginfo") == false);
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue1"));
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue2"));
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue3"));
    assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->LPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->LPop("lpushkey2", value) == false);
    RedisMgr::GetInstance()->Close();
}

int main(int argc, char* argv[])
{
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
        net::io_context ioc{ 1 };
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
#ifdef _WIN32
        signals.add(SIGBREAK);
#endif
        auto gate_request = gate::CreateProductionGateRequest();
        auto logic = std::make_shared<LogicSystem>(*gate_request);
        auto server = std::make_shared<CServer>(ioc, port, logic);
        auto pool = AsioIOServicePool::GetInstance();

        signals.async_wait([&ioc, pool, server](const boost::system::error_code& err, int signal_number) {
            if (err) {
                return;
            }
            SPDLOG_INFO("GateServer shutting down, signal={}", signal_number);
            server->Stop();
            pool->stop();
            ioc.stop();
            });

        try {
            server->Start();
            SPDLOG_INFO("GateServer listening on port={}", port);
            ioc.run();
        }
        catch (...) {
            server->Stop();
            pool->stop();
            throw;
        }
        server->Stop();
        pool->stop();
        SPDLOG_INFO("GateServer stopped");
        logger->Close();
    }
    catch (const std::exception& e) {
        std::cerr << "GateServer startup error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
