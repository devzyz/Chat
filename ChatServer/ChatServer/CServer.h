#pragma once
#include "CSession.h"
#include <boost/asio.hpp>
#include <atomic>
#include <unordered_map>
#include <vector>

class SessionLifecycleCoordinator;
class UserSessionDirectory;
namespace chat_transport {

/** @brief 在 Server strand 接收连接并持有会话；异步入口要求 shared_ptr 所有权及运行中的 I/O。 */
class CServer : public std::enable_shared_from_this<CServer> {
public:
    using ContextSource = std::function<boost::asio::io_context&()>;
    /**
     * @brief 同步绑定数字回环地址并监听；port 为 0 时分配端口，依赖无效或绑定失败抛异常。
     * @note contexts 可为每个会话选择 io，空时复用传入 io；所有 io 必须存活到停止回调完成。
     */
    CServer(boost::asio::io_context& io, std::string address, unsigned short port,
        std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
        std::shared_ptr<UserSessionDirectory> directory, CSession::Submit submit,
        ContextSource contexts = {});
    /** @brief 请求启动接收循环；已有停止请求时返回 false，否则返回 true，不等待 accept 完成。 */
    bool Start();
    /**
     * @brief 幂等请求停止接收并关闭所有会话，在 accept 与会话 I/O/绑定排空后于 Server strand 回调。
     * @note 可重复传入 completion，每个非空回调各执行一次且不得阻塞或抛异常；在线存储清理另由 Drain 等待。
     */
    void Stop(std::function<void()> completion = {});
    /** @brief 供生命周期协调器在会话排空后投递所有权移除；未知 id 无副作用。 */
    void RemoveSession(const SessionId& id);
    /** @brief 返回已请求启动且未请求停止的原子快照，不证明依赖服务健康。 */
    bool Ready() const noexcept { return _ready.load() && !_stop_requested.load(); }
    /** @brief 查询 accept 和全部会话已排空的停止标志，不涵盖在线存储工作队列。 */
    bool Stopped() const noexcept { return _stopped.load(); }
    /** @brief 返回构造时绑定的数字回环地址副本。 */
    std::string BoundAddress() const { return _address; }
    /** @brief 返回实际监听端口，包括传入 0 时由系统分配的端口。 */
    unsigned short BoundPort() const noexcept { return _port; }
    /** @brief 返回 Server 持有的会话数快照，包含尚未排空的关闭中会话。 */
    std::size_t ConnectionCount() const noexcept { return _connection_count.load(); }
private:
    /** @brief 在 Server strand 接收下一连接，成功后持有并启动会话，停止时不再续接。 */
    void Accept();
    /** @brief 在 Server strand 检查停止条件，释放 work guard 并调用已登记的停止回调。 */
    void CompleteStop();
    boost::asio::io_context& _io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _work;
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;
    boost::asio::ip::tcp::acceptor _acceptor;
    std::shared_ptr<SessionLifecycleCoordinator> _lifecycle;
    std::shared_ptr<UserSessionDirectory> _directory;
    CSession::Submit _submit;
    ContextSource _contexts;
    std::unordered_map<SessionId, std::shared_ptr<CSession>> _sessions;
    std::vector<std::function<void()>> _stop_completions;
    bool _started = false;
    bool _stopping = false;
    bool _accept_pending = false;
    std::atomic<bool> _ready{false};
    std::atomic<bool> _stop_requested{false};
    std::atomic<bool> _stopped{false};
    std::atomic<std::size_t> _connection_count{0};
    const std::string _address;
    unsigned short _port;
};

} // namespace chat_transport
