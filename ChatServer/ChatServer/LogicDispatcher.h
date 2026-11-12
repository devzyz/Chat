#pragma once

#include "DependencyExecutor.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class CSession;

/** @brief 持有待分发消息及来源会话的强引用，排队期间保持会话存活。 */
struct LogicMessage {
    std::shared_ptr<CSession> session;
    std::int16_t id;
    std::string body;
    int received_uid = 0;            // 收帧时的认证快照，等待期间的登录不能追认旧请求。
    std::size_t connection_hash = 0; // 服务端在建连时从不可变 SessionId 计算。
};

// 按连接固定分片；每片单线程执行完整请求的就绪步骤，阻塞 I/O 由共享依赖池承担。
class LogicDispatcher {
public:
    using Handler = std::function<bool(const LogicMessage&)>;

    // 默认两个业务分片，handler 可被不同分片并发调用。生产使用进程共享依赖池。
    explicit LogicDispatcher(
        Handler handler,
        std::shared_ptr<DependencyExecutor> dependencies = std::make_shared<DependencyExecutor>(),
        std::size_t business_workers = 2);
    /** @brief 停止接收并等待已接受消息排空；不能在 handler 所在线程析构。 */
    ~LogicDispatcher();

    /** @brief 禁止复制拥有队列和工作线程的分发器。 */
    LogicDispatcher(const LogicDispatcher&) = delete;
    /** @brief 禁止复制赋值，避免混淆任务和工作线程所有权。 */
    LogicDispatcher& operator=(const LogicDispatcher&) = delete;

    /** @brief 同步返回入队结果；Full/Closed 不入队，Accepted 不代表业务处理成功。 */
    LogicSubmitResult Submit(LogicMessage message);
    /** @brief 幂等停止入队并排空队列后 join；必须从工作线程之外调用，不可与自身析构竞争。 */
    void Stop();

    using Completion = std::function<void()>;
    using ExternalWork = std::function<Completion()>;
    using Resume = std::function<void(Completion)>;

    // 从分发线程投递依赖步骤；关联连接时保序，完成和异常回到分发线程。已提交的通知走 QueueNotification。
    LogicSubmitResult SubmitExternal(
        ExternalTask kind,
        std::shared_ptr<CSession> session,
        ExternalWork work,
        Completion failed,
        std::function<bool()> sql_check = {});
    // 暂停当前请求，在原分片恢复绑定结果。调用方必须恰好一次交回完成通知。
    Resume AwaitCompletion(std::shared_ptr<CSession> session);

protected:
    // 从分发线程投递已提交的通知，路由在 Redis 上执行，返回可选的出站 RPC 工作。
    void QueueNotification(ExternalWork route);

private:
    // 串行启动下一笔通知，前一笔本地发送或 RPC 已结束。
    void StartNotification(ExternalWork route);
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
