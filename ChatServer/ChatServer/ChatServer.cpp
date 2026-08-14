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
#include <stdexcept>
#include "LogicSystem.h"
#include "RedisMgr.h"
#include "LogMgr.h"

int main(int argc, char* argv[])
{
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--config")) {
        std::cerr << "Usage: ChatServer.exe [--config <path>]" << std::endl;
        return EXIT_FAILURE;
    }
    if (argc == 3) {
        ConfigMgr::SetConfigPath(argv[2]);
    }
    try {
        auto logger = LogMgr::GetInstance();
        if (!logger->InitLogMgr()) {
            std::cerr << "ChatServer failed to initialize logging." << std::endl;
            return EXIT_FAILURE;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "ChatServer configuration error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    auto& configMgr = ConfigMgr::GetInstance();
    // chatserver服务器启动后，将连接数更新到redis中
    auto self_server_name = configMgr["SelfServer"]["Name"];
    boost::asio::io_context io_context;
    std::shared_ptr<AsioIOServicePool> pool;
    std::shared_ptr<CServer> p_server;
    std::shared_ptr<RedisMgr> redis;
    std::unique_ptr<grpc::Server> server;
    std::thread grpc_server_thread;
    bool login_count_registered = false;
    try {
        pool = AsioIOServicePool::GetInstance();
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 先完成所有本地端口绑定。任何端口冲突都必须在启动线程和登记Redis状态前失败。
        auto port_str = configMgr["SelfServer"]["Port"];
        p_server = std::make_shared<CServer>(io_context, std::stoi(port_str));

        // chatserver对应的grpc服务器地址
        std::string server_address = configMgr["SelfServer"]["Host"] + ":" + configMgr["SelfServer"]["RPCPort"];
        ChatServiceImpl service;
        grpc::ServerBuilder builder;
        // 添加监听的端口，以及注册grpc服务
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        // 构建并启动gRPC服务器
        server = builder.BuildAndStart();
        if (!server) {
            throw std::runtime_error("failed to listen on gRPC address " + server_address);
        }
        SPDLOG_INFO("chat grpc Server listening on {}\n", server_address);

        p_server->init(); // 启动定时器

        // 所有监听端口均已成功绑定后，才向Redis登记本实例。
        redis = RedisMgr::GetInstance();
        login_count_registered = redis->HSet(LOGIN_COUNT, self_server_name, "0");

        // 创建一个单独的线程等待grpc
        grpc_server_thread = std::thread([&server]() {
            server->Wait();
            });

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
        server->Shutdown();
        grpc_server_thread.join(); // 等待线程结束
        if (login_count_registered) {
            redis->HDel(LOGIN_COUNT, self_server_name);
            login_count_registered = false;
        }
        redis->Close();
    }
    catch (const std::exception& e) {
        if (p_server) {
            p_server->stop();
        }
        io_context.stop();
        if (pool) {
            pool->stop();
        }
        if (server) {
            server->Shutdown();
        }
        if (grpc_server_thread.joinable()) {
            grpc_server_thread.join();
        }
        if (redis) {
            if (login_count_registered) {
                redis->HDel(LOGIN_COUNT, self_server_name);
            }
            redis->Close();
        }
        SPDLOG_ERROR("ChatServer exception: {}", e.what());
        std::cerr << "ChatServer startup error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
