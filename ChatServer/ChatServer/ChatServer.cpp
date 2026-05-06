#include <mutex>
#include <thread>
#include "ConfigMgr.h"
#include <iostream>
#include <csignal>
#include "AsioIOServicePool.h"

int main()
{
    try {
        auto& configMgr = ConfigMgr::GetInstance();
        auto pool = AsioIOServicePool::GetInstance();
    }
    catch (std::exception& e) {
        std::cerr << "Exception : " << e.what();
        return EXIT_FAILURE;
    }

    return 0;
}