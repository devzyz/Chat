#include <mutex>
#include <thread>
#include "ConfigMgr.h"
#include <iostream>
#include <csignal>
#include "AsioIOServicePool.h"
#include "CServer.h"

int main()
{
    try {
        auto& configMgr = ConfigMgr::GetInstance();
        auto pool = AsioIOServicePool::GetInstance();
        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

        signals.async_wait([&io_context, pool](auto, auto) {
            io_context.stop();
            pool->stop();
            });

        auto port_str = configMgr["SelfServer"]["Port"];
        CServer s(io_context, atoi(port_str.c_str()));
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception : " << e.what();
        return EXIT_FAILURE;
    }

    return 0;
}