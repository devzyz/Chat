#pragma once
#include "ResourceStore.h"
#include <boost/asio.hpp>
#include <functional>
#include <memory>

namespace resource {
/** @brief 借用资源存储并拥有 HTTP 监听状态；调用者须保证存储和业务回调依赖覆盖服务生命周期。 */
class ResourceHttpServer {
public:
    using Authenticate = std::function<bool(int, const std::string&)>;
    using CanRead = std::function<bool(int, const Metadata&)>;
    using Publish = std::function<void(const Metadata&)>;
    using GetAvatar = std::function<std::string(int)>;
    using SetAvatar = std::function<void(int, const std::string&)>;
    /** @brief 绑定并监听指定端点，创建存储工作线程；绑定错误抛异常，存储和回调依赖须活过所有会话。 */
    ResourceHttpServer(boost::asio::io_context& context, const std::string& host, unsigned short port,
                       ResourceStore& store, Authenticate authenticate, CanRead can_read = {}, Publish publish = {},
                       GetAvatar get_avatar = {}, SetAvatar set_avatar = {});
    /** @brief 等待存储工作线程结束；不自动停止监听，调用方须先 Stop 并完成所属 I/O 回调清理。 */
    ~ResourceHttpServer();
    /** @brief 在构造时已绑定的监听器上安排首次异步接收。 */
    void Start();
    /** @brief 幂等关闭监听器和当前会话；须在所属 I/O 线程调用，不等待存储工作或异步回调完成。 */
    void Stop();
    /** @brief 返回资源 HTTP 监听器实际绑定的端口。 */
    unsigned short Port() const;
private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
};
}
