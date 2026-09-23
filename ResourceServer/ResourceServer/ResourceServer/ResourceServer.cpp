#include "ResourceHttpServer.h"
#include "../../../common/asio/IOServicePool.h"
#include "../../../common/grpc/GrpcClientRuntime.h"
#include "../../../common/resource/ResourceCatalog.h"
#include "status.grpc.pb.h"
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
        if (argc == 3 && std::string(argv[1]) == "--config") path = argv[2];
        else if (argc != 1) throw std::invalid_argument("usage: ResourceServer [--config path]");
        boost::property_tree::ptree config;
        boost::property_tree::read_ini(path, config);
        const auto port = config.get<unsigned int>("ResourceServer.Port");
        if (!port || port > 65535) throw std::invalid_argument("invalid ResourceServer.Port");
        std::filesystem::create_directories(config.get<std::string>("Log.LogDir", "logs"));
        auto logger = spdlog::rotating_logger_mt("ResourceServer",
            (std::filesystem::path(config.get<std::string>("Log.LogDir", "logs")) / "ResourceServer.log").string(),
            5 * 1024 * 1024, 2);
        spdlog::set_default_logger(logger);
        auto storage_root = std::filesystem::path(config.get<std::string>("ResourceServer.StorageRoot"));
        if (storage_root.is_relative()) storage_root = std::filesystem::absolute(path).parent_path() / storage_root;
        resource::ResourceStore store(storage_root,
            config.get<std::uint64_t>("ResourceServer.MaxFileBytes", 8ull * 1024 * 1024 * 1024));
        resource::ResourceCatalog catalog(config.get<std::string>("Mysql.Host") + ":" + config.get<std::string>("Mysql.Port"),
            config.get<std::string>("Mysql.User"), config.get<std::string>("Mysql.Password", ""),
            config.get<std::string>("Mysql.Schema"));
        const auto endpoint = config.get<std::string>("StatusServer.Host") + ":" + config.get<std::string>("StatusServer.Port");
        rpc::BoundedPool<message::StatusService::Stub> status(2, std::chrono::milliseconds(1000), /** @brief 按配置端点创建 StatusService stub。 */ [endpoint] {
            return message::StatusService::NewStub(grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
        });
        common::IOServicePool pool(1);
        auto& context = pool.GetIOService();
        resource::ResourceHttpServer server(context, config.get<std::string>("ResourceServer.Host"),
            static_cast<unsigned short>(port), store,
            /** @brief 通过 Status 登录 RPC 验证请求 UID 与 Token。 */ [&status](int uid, const std::string& token) {
                message::LoginReq request; request.set_uid(uid); request.set_token(token);
                auto result = rpc::InvokeUnary<decltype(status), message::LoginReq, message::LoginRsp>(
                    status, request, std::chrono::milliseconds(3000),
                    /** @brief 执行已设置截止时间的 Status 登录调用。 */ [](auto& stub, auto& context, const auto& input, auto& output) { return stub.Login(&context, input, &output); });
                return result && result.response.error() == 0 && result.response.uid() == uid;
            }, /** @brief 通过共享目录检查用户读取资源的权限。 */ [&catalog](int uid, const resource::Metadata& metadata) { return catalog.CanRead(uid, metadata.id); },
            /** @brief 把上传完成的元数据发布到共享目录。 */ [&catalog](const resource::Metadata& metadata) {
                catalog.Publish(metadata.id, metadata.owner, metadata.name, metadata.media_type, metadata.size, metadata.sha256);
            }, /** @brief 查询用户已发布头像的资源 ID。 */ [&catalog](int uid) { return catalog.GetAvatar(uid); },
            /** @brief 更新用户头像并由目录再次核验资源归属。 */ [&catalog](int uid, const std::string& id) { catalog.SetAvatar(uid, id); });
        std::promise<void> stopped;
        auto future = stopped.get_future();
        boost::asio::signal_set signals(context, SIGINT, SIGTERM);
#ifdef _WIN32
        signals.add(SIGBREAK);
#endif
        signals.async_wait(/** @brief 停止资源服务并通知停服等待者。 */ [&](auto, auto) { server.Stop(); stopped.set_value(); });
        boost::asio::post(context, /** @brief 在服务执行器开始资源 HTTP 监听。 */ [&] { server.Start(); });
        SPDLOG_INFO("ResourceServer listening on port {}", port);
        future.wait();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ResourceServer startup failed: " << error.what() << '\n';
        return 1;
    }
}
