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
class CSession : public std::enable_shared_from_this<CSession> {
public:
    using Submit = std::function<LogicSubmitResult(LogicMessage)>;
    CSession(boost::asio::io_context& io, std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
        std::shared_ptr<UserSessionDirectory> directory, Submit submit);
    ~CSession() = default;
    boost::asio::ip::tcp::socket& Socket(); // Acceptor only, before Start.
    const SessionId& Id() const noexcept { return _id; }
    int AuthenticatedUid() const noexcept { return _authenticated_uid.load(); }
    void Start();
    void Send(SessionFrame frame, SendCompletion completion = {});
    void Send(const std::string& body, std::uint16_t id, SendCompletion completion = {});
    void Close(SessionCloseReason reason = SessionCloseReason::LocalRequest);
    void BindAuthenticatedUser(int uid, BindCompletion completion);
    void Inspect(std::function<void(SessionState, std::optional<SessionCloseReason>)> completion);
private:
    friend class SessionLifecycleCoordinator;
    void FinishBinding(int uid, SessionBindResult result, BindCompletion completion,
        std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(bool)> committed);
    void BeginClosing(SessionCloseReason reason);
    void ReleaseIfIdle();
    void ReadHeader();
    void ReadBody(std::uint16_t id, std::size_t length);
    void OnFrame(std::uint16_t id);
    void StartWrite();
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
