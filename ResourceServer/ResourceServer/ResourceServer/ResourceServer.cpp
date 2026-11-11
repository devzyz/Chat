#include "ResourceHttpServer.h"
#include "../../../common/asio/IOServicePool.h"
#include "../../../common/auth/RedisToken.h"
#include "../../../common/resource/ResourceCatalog.h"
#include <boost/property_tree/ini_parser.hpp>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <csignal>
#include <cstdlib>
#include <future>
#include <iostream>

/** @brief 解析启动配置并初始化本服务依赖，发布就绪信息后运行事件循环，按信号或错误执行关闭流程。 */
int main(int argc, char** argv) {
    try {
        std::string path = std::getenv("CHAT_CONFIG") ? std::getenv("CHAT_CONFIG") : "config.ini";
        if (argc == 3 && std::string(argv[1]) == "--config")
            path = argv[2];
        else if (argc != 1)
            throw std::invalid_argument("usage: ResourceServer [--config path]");
        boost::property_tree::ptree config;
        boost::property_tree::read_ini(path, config);
        const auto port = config.get<unsigned int>("ResourceServer.Port");
        if (!port || port > 65535)
            throw std::invalid_argument("invalid ResourceServer.Port");
        std::filesystem::create_directories(config.get<std::string>("Log.LogDir", "logs"));
        auto logger = spdlog::rotating_logger_mt(
            "ResourceServer",
            (std::filesystem::path(config.get<std::string>("Log.LogDir", "logs")) / "ResourceServer.log").string(),
            5 * 1024 * 1024,
            2);
        spdlog::set_default_logger(logger);
        auto storage_root = std::filesystem::path(config.get<std::string>("ResourceServer.StorageRoot"));
        if (storage_root.is_relative())
            storage_root = std::filesystem::absolute(path).parent_path() / storage_root;
        resource::ResourceStore store(
            storage_root,
            config.get<std::uint64_t>("ResourceServer.MaxFileBytes", 8ull * 1024 * 1024 * 1024),
            config.get<std::uint64_t>("ResourceServer.MaxTotalBytes", 64ull * 1024 * 1024 * 1024),
            config.get<std::uint64_t>("ResourceServer.MaxOwnerBytes", 16ull * 1024 * 1024 * 1024));
        resource::ResourceCatalog catalog(
            config.get<std::string>("Mysql.Host") + ":" + config.get<std::string>("Mysql.Port"),
            config.get<std::string>("Mysql.User"),
            config.get<std::string>("Mysql.Password", ""),
            config.get<std::string>("Mysql.Schema"));
        chat_redis::RedisPool redis(
            config.get<std::string>("Redis.Host"),
            config.get<int>("Redis.Port"),
            config.get<std::string>("Redis.Password", ""),
            2);
        common::IOServicePool pool(1);
        auto& context = pool.GetIOService();
        resource::ResourceHttpServer server(
            context,
            config.get<std::string>("ResourceServer.Host"),
            static_cast<unsigned short>(port),
            store,
            // 读取当前 Redis 凭据，不延长登录有效期。
            [&redis](int uid, const std::string& token) {
                const auto result = chat_auth::ValidateToken(redis, uid, token);
                if (result == chat_auth::TokenResult::Unavailable)
                    SPDLOG_WARN("credential store unavailable");
                return result == chat_auth::TokenResult::Valid;
                // 通过共享目录检查用户读取资源的权限。
            },
            [&catalog](int uid, const resource::Metadata& metadata) {
                return catalog.CanRead(uid, metadata.id);
            },
            // 把上传完成的元数据发布到共享目录。
            [&catalog](const resource::Metadata& metadata) {
                catalog.Publish(
                    metadata.id,
                    metadata.owner,
                    metadata.name,
                    metadata.media_type,
                    metadata.size,
                    metadata.sha256);
                // 查询用户已发布头像的资源 ID。
            },
            [&catalog](int uid) {
                return catalog.GetAvatar(uid);
            },
            // 更新用户头像并由目录再次核验资源归属。
            [&catalog](int uid, const std::string& id) {
                catalog.SetAvatar(uid, id);
            },
            // 借用时连接池验证认证及 PING，同时检查目录 schema。
            [&catalog, &redis] {
                if (!catalog.IsReady())
                    return false;
                auto* connection = redis.Borrow(std::chrono::milliseconds(1000));
                if (!connection)
                    return false;
                redis.Return(connection);
                return true;
            });
        std::promise<void> stopped;
        auto future = stopped.get_future();
        boost::asio::signal_set signals(context, SIGINT, SIGTERM);
#ifdef _WIN32
        signals.add(SIGBREAK);
#endif
        // 停止资源服务并通知停服等待者。
        signals.async_wait([&](auto, auto) {
            server.Stop();
            stopped.set_value();
        });
        // 在服务执行器开始资源 HTTP 监听。
        boost::asio::post(context, [&] {
            server.Start();
        });
        SPDLOG_INFO("ResourceServer listening on port {}", port);
        future.wait();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ResourceServer startup failed: " << error.what() << '\n';
        return 1;
    }
}
