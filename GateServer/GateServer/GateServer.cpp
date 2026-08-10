#include "CServer.h"
#include "const.h"
#include "ConfigMgr.h"
#include <hiredis/hiredis.h>
#include "RedisMgr.h"
#include "LogMgr.h"

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
    if (argc == 3 && std::string(argv[1]) == "--config") {
        ConfigMgr::SetConfigPath(argv[2]);
    }
    //TestRedisMgr();
    auto logger = LogMgr::GetInstance();
    if (!logger->InitLogMgr()) {
        return EXIT_FAILURE;
    }
    ConfigMgr& gCfgMgr = ConfigMgr::GetInstance();
    // 获取当前服务的端口信息
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());

    try {
        unsigned short port = static_cast<unsigned short> (gate_port);
        net::io_context ioc{ 1 };
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        // 异步等待，当接收到SIGINT, SIGTERM信号后，触发后面的回调函数
        signals.async_wait([&ioc](const boost::system::error_code& err, int signal_number) {
            if (err) {
                return;
            }
            ioc.stop();
            });
        
        std::make_shared<CServer>(ioc, port)->Start();
        SPDLOG_INFO("GateServer listening on port={}", port);
        ioc.run();
    }
    catch (std::exception& e) {
        SPDLOG_ERROR("GateServer exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
