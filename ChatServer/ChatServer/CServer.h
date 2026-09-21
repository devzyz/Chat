#pragma once
#include "CSession.h"
#include <boost/asio.hpp>
#include <atomic>
#include <unordered_map>
#include <vector>

class SessionLifecycleCoordinator;
class UserSessionDirectory;
class CServer : public std::enable_shared_from_this<CServer> {
public:
    using ContextSource = std::function<boost::asio::io_context&()>;
    CServer(boost::asio::io_context& io, unsigned short port,
        std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
        std::shared_ptr<UserSessionDirectory> directory, CSession::Submit submit,
        ContextSource contexts = {});
    void Start();
    void Stop(std::function<void()> completion = {});
    void RemoveSession(const SessionId& id);
    bool Ready() const noexcept { return _ready.load(); }
    std::string BoundAddress() const { return _address; }
    unsigned short BoundPort() const noexcept { return _port; }
    std::size_t ConnectionCount() const noexcept { return _connection_count.load(); }
private:
    void Accept();
    void CompleteStop();
    boost::asio::io_context& _io;
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
    std::atomic<std::size_t> _connection_count{0};
    const std::string _address;
    const unsigned short _port;
};
