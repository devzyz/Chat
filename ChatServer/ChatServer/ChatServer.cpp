#include "ConfigMgr.h"
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "RedisMgr.h"
#include "RedisUserPresenceStore.h"
#include "SessionLifecycleCoordinator.h"
#include "ChatServiceImpl.h"
#include "ChatGrpcClient.h"
#include "LogicSystem.h"
#include "LogMgr.h"
#include <csignal>
#include <iostream>
#include <stdexcept>

/** @brief 解析启动配置并初始化本服务依赖，发布就绪信息后运行事件循环，按信号或错误执行关闭流程。 */
int main(int argc, char* argv[]) {
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--config")) {
        std::cerr << "Usage: ChatServer.exe [--config <path>]" << std::endl;
        return EXIT_FAILURE;
    }
    boost::asio::io_context io;
    boost::asio::steady_timer count_timer(io);
    boost::asio::thread_pool maintenance(1);
    std::shared_ptr<AsioIOServicePool> pool;
    std::shared_ptr<RedisMgr> redis;
    std::shared_ptr<SessionLifecycleCoordinator> lifecycle;
    std::shared_ptr<chat_transport::CServer> tcp;
    std::unique_ptr<LogicSystem> logic;
    std::unique_ptr<ChatServiceImpl> service;
    std::unique_ptr<grpc::Server> rpc;
    std::string server_id;
    bool registered = false;
    bool stopping = false;
    bool tcp_stopped = false;
    auto shutdown = /** @brief 幂等取消维护计时并启动 TCP 停服。 */ [&] {
        if (stopping) return;
        stopping = true;
        boost::system::error_code ignored;
        count_timer.cancel();
        if (tcp) tcp->Stop(/** @brief 在会话关闭及 I/O 取消完成后退出主事件循环。 */ [&] { tcp_stopped = true; io.stop(); });
        else { tcp_stopped = true; io.stop(); }
    };
    auto finish = /** @brief 排空连接事件后等待工作线程与 RPC 关闭。 */ [&] {
        // Run the acceptor executor until all sessions have completed close and cancelled I/O.
        if (!tcp_stopped) { io.restart(); io.run(); }
        // Blocking worker/RPC joins run on the owner after the acceptor has stopped, not in its handler.
        if (rpc) rpc->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5));
        if (logic) logic->Stop();
        if (lifecycle) lifecycle->Drain();
        maintenance.join();
        if (pool) pool->Stop();
        if (rpc) rpc->Wait();
        if (registered) { redis->HDel(LOGIN_COUNT, server_id); registered = false; }
        if (redis) redis->Close();
    };
    try {
        if (argc == 3) ConfigMgr::SetConfigPath(argv[2]);
        if (!LogMgr::GetInstance()->InitLogMgr()) throw std::runtime_error("logging initialization failed");
        auto& config = ConfigMgr::GetInstance();
        server_id = config["SelfServer"]["Name"];
        const auto port_text = config["SelfServer"]["Port"];
        std::size_t parsed = 0;
        const int port = std::stoi(port_text, &parsed);
        if (parsed != port_text.size() || port < 1 || port > 65535) throw std::runtime_error("invalid TCP port");
        auto directory = std::make_shared<UserSessionDirectory>();
        // The adapter resolves Redis lazily: bind both ports before connecting to dependencies.
        auto presence = std::make_shared<RedisUserPresenceStore>();
        lifecycle = std::make_shared<SessionLifecycleCoordinator>(directory, presence, server_id,
            /** @brief 向旧用户所在实例发送带会话标识的踢出请求。 */ [](int uid, const chat_session::UserPresence& old) {
                message::KickUserReq request;
                request.set_uid(uid);
                request.set_session_id(old.session_id);
                const auto response = ChatGrpcClient::GetInstance()->NotifyOtherKickUser(old.server_id, request);
                if (response.error() != ErrorCodes::Success) SPDLOG_WARN("remote replacement failed, uid={}", uid);
            });
        logic = std::make_unique<LogicSystem>(directory, presence);
        tcp = std::make_shared<chat_transport::CServer>(io, config["SelfServer"]["Host"], static_cast<unsigned short>(port), lifecycle, directory,
            /** @brief 把入站消息交给有界业务分发器。 */ [&](LogicMessage message) { return logic->Submit(std::move(message)); },
            /** @brief 从 Asio 池选择会话执行器。 */ [&]() -> boost::asio::io_context& { return pool->GetIOService(); });
        lifecycle->AttachServer(tcp);
        service = std::make_unique<ChatServiceImpl>(directory, lifecycle);
        grpc::ServerBuilder builder;
        builder.AddListeningPort(config["SelfServer"]["Host"] + ":" + config["SelfServer"]["RPCPort"],
            grpc::InsecureServerCredentials());
        builder.RegisterService(service.get());
        rpc = builder.BuildAndStart();
        if (!rpc) throw std::runtime_error("failed to listen on gRPC address");
        redis = RedisMgr::GetInstance();
        presence->AttachRedis(redis);
        pool = AsioIOServicePool::GetInstance();
        registered = redis->HSet(LOGIN_COUNT, server_id, "0");
        tcp->Start();
        std::function<void()> update_count;
        update_count = /** @brief 安排下一轮连接计数发布。 */ [&] {
            count_timer.expires_after(std::chrono::seconds(60));
            count_timer.async_wait(/** @brief 计时成功且未停服时把计数更新交给维护执行器。 */ [&](boost::system::error_code error) {
                if (error || stopping) return;
                const auto count = tcp->ConnectionCount();
                boost::asio::post(maintenance, /** @brief 将当前连接数写入 Redis 选服统计。 */ [redis, server_id, count] {
                    if (!redis->HSet(LOGIN_COUNT, server_id, std::to_string(count)))
                        SPDLOG_WARN("connection count publication failed");
                });
                update_count();
            });
        };
        update_count();
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait(/** @brief 接收退出信号并启动统一停服流程。 */ [&](boost::system::error_code error, int) { if (!error) shutdown(); });
        io.run();
        shutdown();
        finish();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        shutdown();
        finish();
        std::cerr << "ChatServer startup error: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }
}
