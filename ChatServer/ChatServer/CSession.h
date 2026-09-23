#pragma once
#include "SessionTypes.h"
#include "LogicDispatcher.h"
#include "ChatFrameCodec.h"
#include <boost/asio.hpp>
#include <atomic>
#include <deque>
#include <memory>
#include <optional>

class SessionLifecycleCoordinator;
class UserSessionDirectory;
/**
 * @brief 在独立 strand 上管理 TCP 会话、收发队列、心跳与关闭；异步调用要求 shared_ptr 所有权。
 * @note io 和生命周期协调器必须存活到回调排空；原子查询是快照，Active 不代表已认证。
 */
class CSession : public std::enable_shared_from_this<CSession> {
public:
    using Submit = std::function<LogicSubmitResult(LogicMessage)>;
    /** @brief 创建未启动会话；依赖必须有效，submit 在会话 strand 同步调用，不应阻塞。 */
    CSession(boost::asio::io_context& io, std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
        std::shared_ptr<UserSessionDirectory> directory, Submit submit);
    /** @brief 默认释放成员资源；正常关闭及所有权移除由 Close 的异步流程完成。 */
    ~CSession() = default;
    /** @brief 借出 socket 供 acceptor 在 Start 前接入；引用只在会话存活期间有效。 */
    boost::asio::ip::tcp::socket& Socket(); // Acceptor only, before Start.
    /** @brief 返回不可变会话标识的借用引用，有效期与会话相同。 */
    const SessionId& Id() const noexcept { return _id; }
    /** @brief 返回已绑定 UID 的原子快照；未绑定或进入关闭时为 0。 */
    int AuthenticatedUid() const noexcept { return _authenticated_uid.load(); }
    /** @brief 查询客户端是否启用送达及已读回执能力。 */
    bool SupportsReceipts() const noexcept { return _receipts.load(); }
    /** @brief 原子更新协商后的回执能力，不改变认证状态。 */
    void EnableReceipts(bool enabled) noexcept { _receipts.store(enabled); }
    /** @brief 投递首次启动至 strand，开始收帧和心跳；重复启动或关闭后启动无效。 */
    void Start();
    /**
     * @brief 投递帧至 strand，并在该 strand 回调入队结果；Accepted 不代表写完或对端收到。
     * @note 队列满返回 Full；非 Active 或超长帧返回 NotActive，超长帧还会关闭会话。
     * completion 可为空；非空时不得阻塞或抛异常，执行依赖 io 持续运行。
     */
    void Send(SessionFrame frame, SendCompletion completion = {});
    /** @brief 复制 body 并按 id 构造帧，入队结果及回调约束与帧重载相同。 */
    void Send(const std::string& body, std::uint16_t id, SendCompletion completion = {});
    /** @brief 异步幂等关闭，保留首次原因；返回不表示在途 I/O、绑定和远端清理已经结束。 */
    void Close(SessionCloseReason reason = SessionCloseReason::LocalRequest);
    /**
     * @brief 异步发布在线位置并绑定 uid，在 strand 调用必填 completion；回调不得阻塞或抛异常。
     * @note 非 Active 返回 NotActive；无效 UID、已绑定或绑定中返回 AlreadyBound；存储失败返回
     * Unavailable。Bound 表示本地登记完成，跨服替换通知可能仍未完成。
     */
    void BindAuthenticatedUser(int uid, BindCompletion completion);
    /** @brief 在 strand 将状态及首次关闭原因交给必填回调；回调不得阻塞或抛异常。 */
    void Inspect(std::function<void(SessionState, std::optional<SessionCloseReason>)> completion);
private:
    friend class SessionLifecycleCoordinator;
    /** @brief 将发布结果送回 strand；取消或关闭时拒绝绑定，先通知 committed 再调用业务 completion。 */
    void FinishBinding(int uid, SessionBindResult result, BindCompletion completion,
        std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(bool)> committed);
    /** @brief 仅在 strand 首次进入 Closing，注销本地身份并取消 socket、心跳及待发队列。 */
    void BeginClosing(SessionCloseReason reason);
    /** @brief 在 strand 确认关闭且 I/O、绑定全部结束后，仅一次请求 Server 释放所有权。 */
    void ReleaseIfIdle();
    /** @brief 在 strand 启动定长帧头读取，校验通过后读取消息体，否则关闭。 */
    void ReadHeader();
    /** @brief 在 strand 读取已校验 length 字节；零长度直接分发，失败关闭。 */
    void ReadBody(std::uint16_t id, std::size_t length);
    /** @brief 在 strand 提交完整帧；队列满丢弃本帧并继续读，分发关闭或异常则关闭会话。 */
    void OnFrame(std::uint16_t id);
    /** @brief 在 strand 串行写队首，回调独立持有缓冲；成功续写，失败关闭。 */
    void StartWrite();
    /** @brief 在 strand 启动心跳检查；按最近收到完整帧的时刻判断超时并关闭。 */
    void ArmHeartbeat();
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;
    boost::asio::ip::tcp::socket _socket;
    boost::asio::steady_timer _heartbeat;
    std::shared_ptr<SessionLifecycleCoordinator> _lifecycle;
    std::shared_ptr<UserSessionDirectory> _directory;
    Submit _submit;
    const SessionId _id;
    SessionState _state = SessionState::Created;
    std::optional<SessionCloseReason> _close_reason;
    std::atomic<bool> _receipts{false};
    std::atomic<int> _authenticated_uid{0}; // Read-only snapshot for business threads.
    int _binding_uid = 0;
    bool _binding = false;
    bool _released = false;
    std::size_t _io_pending = 0;
    bool _write_active = false;
    ChatFrameCodec::HeaderBytes _header{};
    std::string _body;
    std::deque<std::shared_ptr<std::string>> _frames;
    std::chrono::steady_clock::time_point _last_activity;
};
