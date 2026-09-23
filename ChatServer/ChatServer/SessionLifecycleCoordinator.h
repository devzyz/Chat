#pragma once
#include "UserPresenceStore.h"
#include "UserSessionDirectory.h"
#include <boost/asio.hpp>
#include <memory>

namespace chat_transport { class CServer; }
class CSession;
/** @brief 用单工作线程串行发布及清理在线位置；弱引用 Server，绑定任务临时持有会话。 */
class SessionLifecycleCoordinator {
public:
    using Kick = std::function<void(int, const chat_session::UserPresence&)>;
    /** @brief 保存有效的目录、在线存储和实例标识；可选 kick 在工作线程通知旧实例踢出指定会话。 */
    SessionLifecycleCoordinator(std::shared_ptr<UserSessionDirectory> directory,
        std::shared_ptr<UserPresenceStore> presence, std::string server_id, Kick kick = {});
    /** @brief 等待工作队列退出；销毁前应停止任务生产，且不得从工作线程销毁自身。 */
    ~SessionLifecycleCoordinator();
    /** @brief 在会话工作开始前挂接所属 Server 的弱引用，之后不得并发替换。 */
    void AttachServer(std::weak_ptr<chat_transport::CServer> server);
    /** @brief 为已进入绑定流程的会话排队发布位置，再由会话 strand 完成本地登记及必填回调。 */
    void OnAuthenticated(std::shared_ptr<CSession> session, int uid, BindCompletion completion);
    /** @brief 同步条件删除本地映射并异步清理在线位置；存储失败记录日志，不阻塞本地关闭。 */
    void OnClosing(const SessionId& id, int uid);
    /** @brief 关闭 uid 下标识匹配的绑定中或当前会话；空 id 或旧标识不会踢出新会话。 */
    void CloseReplaced(int uid, const SessionId& id);
    /** @brief 向仍存活的 Server 异步请求移除已排空会话的强引用。 */
    void ReleaseOwnership(const SessionId& id);
    /** @brief 由所有者停止生产任务后等待工作队列；必须保留会话 I/O 执行器运行，不可在工作线程调用。 */
    void Drain(); // Owner only, while session I/O executors are still running.
private:
    std::shared_ptr<UserSessionDirectory> _directory;
    std::shared_ptr<UserPresenceStore> _presence;
    std::string _server_id;
    Kick _kick;
    std::weak_ptr<chat_transport::CServer> _server;
    std::mutex _binding_mutex;
    std::unordered_map<SessionId, std::pair<int, std::weak_ptr<CSession>>> _binding_sessions;
    boost::asio::thread_pool _worker{1};
};
