#include "CServer.h"
#include "const.h"
#include "ConfigMgr.h"
#include <hiredis/hiredis.h>
#include "RedisMgr.h"

void TestRedisMgr() {
    assert(RedisMgr::GetInstance()->Connect("192.168.18.128", 6380));
    assert(RedisMgr::GetInstance()->Auth("123456"));
    //assert(RedisMgr::GetInstance()->Set("blogwebsite", "llfc.club"));
    //std::string value = "";
    //assert(RedisMgr::GetInstance()->Get("blogwebsite", value));
    //assert(RedisMgr::GetInstance()->Get("nonekey", value) == false);
    //assert(RedisMgr::GetInstance()->HSet("bloginfo", "blogwebsite", "llfc.club"));
    //assert(RedisMgr::GetInstance()->HGet("bloginfo", "blogwebsite") != "");
    //assert(RedisMgr::GetInstance()->ExistsKey("bloginfo"));
    //assert(RedisMgr::GetInstance()->Del("bloginfo"));
    //assert(RedisMgr::GetInstance()->Del("bloginfo"));
    //assert(RedisMgr::GetInstance()->ExistsKey("bloginfo") == false);
    //assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue1"));
    //assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue2"));
    //assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue3"));
    //assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    //assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    //assert(RedisMgr::GetInstance()->LPop("lpushkey1", value));
    //assert(RedisMgr::GetInstance()->LPop("lpushkey2", value) == false);
    //RedisMgr::GetInstance()->Close();
}

int main()
{
    TestRedisMgr();
    ConfigMgr& gCfgMgr = ConfigMgr::GetInstance();
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());

    try {
        unsigned short port = static_cast<unsigned short> (8080);
        net::io_context ioc{ 1 };
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        signals.async_wait([&ioc](const boost::system::error_code& err, int signal_number) {
            if (err) {
                return;
            }
            ioc.stop();
            });
        
        std::make_shared<CServer>(ioc, port)->Start();
        std::cout << "Gate Server listen on port: " << port << std::endl;
        ioc.run();
    }
    catch (std::exception& e) {
        std::cerr<< "exception : " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
