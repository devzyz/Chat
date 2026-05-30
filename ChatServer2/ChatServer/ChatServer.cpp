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

int main()
{
    auto& configMgr = ConfigMgr::GetInstance();
    // chatserver服务器启动后，将连接数更新到redis中
    auto self_server_name = configMgr["SelfServer"]["Name"];
    try {
        auto pool = AsioIOServicePool::GetInstance();

        // 设置当前server的tcp客户端连接数为0
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
        std::cout << "chat grpc Server listening on " << server_address << std::endl;

        // 创建一个单独的线程等待grpc
        std::thread grpc_server_thread([&server]() {
            server->Wait();
            });

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        // 优雅的退出
        signals.async_wait([&io_context, pool, &server](auto, auto) {
            io_context.stop();
            pool->stop();
            server->Shutdown();
            });

        auto port_str = configMgr["SelfServer"]["Port"];
        CServer s(io_context, atoi(port_str.c_str()));
        io_context.run(); // 通过signals来保活

        // 结束后将一些状态清空
        RedisMgr::GetInstance()->HDel(LOGIN_COUNT, self_server_name);
        RedisMgr::GetInstance()->Close();
        grpc_server_thread.join(); // 等待线程结束
    }
    catch (std::exception& e) {
        std::cerr << "Exception : " << e.what();
        return EXIT_FAILURE;
    }

    return 0;
}