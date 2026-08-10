#include <mutex>
#include <thread>
#include "ConfigMgr.h"
#include <iostream>
#include <csignal>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "RedisMgr.h"
#include "Const.h"
#include "ChatServiceImpl.h"
#include <memory>
#include "LogicSystem.h"
#include "RedisMgr.h"
#include "LogMgr.h"

int main(int argc, char* argv[])
{
    if (argc == 3 && std::string(argv[1]) == "--config") {
        ConfigMgr::SetConfigPath(argv[2]);
    }
    auto logger = LogMgr::GetInstance();
    logger->InitLogMgr();
    auto& configMgr = ConfigMgr::GetInstance();
    // chatserver服务器启动后，将连接数更新到redis中
    auto self_server_name = configMgr["SelfServer"]["Name"];
    try {
        auto pool = AsioIOServicePool::GetInstance();

        // 因为只写本服，所以直接初始化一下
        // 但是状态服务器还是会查找，可能出现先查后写的情况，因为心跳60秒更新一次，允许出现一些小的误差，提高性能
        RedisMgr::GetInstance()->HSet(LOGIN_COUNT, self_server_name, "0");

        // chatserver对应的grpc服务器地址
        std::string server_address = configMgr["SelfServer"]["Host"] + ":" + configMgr["SelfServer"]["RPCPort"];
        ChatServiceImpl service;
        grpc::ServerBuilder builder;
        // 添加监听的端口，以及注册grpc服务
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        // 构建并启动gRPC服务器
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        SPDLOG_INFO("chat grpc Server listening on {}\n", server_address);


        // 创建一个单独的线程等待grpc
        std::thread grpc_server_thread([&server]() {
            server->Wait();
            });

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 创建CServer
        auto port_str = configMgr["SelfServer"]["Port"];
        std::shared_ptr<CServer> p_server = std::make_shared<CServer>(io_context, atoi(port_str.c_str()));
        p_server->init(); // 启动定时器
        // 优雅的退出
        signals.async_wait([&io_context, pool, &server, &p_server](auto, auto) {
            p_server->stop();
            io_context.stop();
            pool->stop();
            server->Shutdown();
            });
        
        LogicSystem::GetInstance()->SetServer(p_server);
        service.SetServer(p_server);
        io_context.run(); // 通过signals来保活

        // 结束后将一些状态清空
        RedisMgr::GetInstance()->HDel(LOGIN_COUNT, self_server_name);
        RedisMgr::GetInstance()->Close();
        grpc_server_thread.join(); // 等待线程结束
    }
    catch (std::exception& e) {
        SPDLOG_ERROR("ChatServer exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
