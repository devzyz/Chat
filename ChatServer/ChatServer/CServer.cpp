#include "CServer.h"
#include "SessionLifecycleCoordinator.h"

namespace chat_transport {

CServer::CServer(boost::asio::io_context& io, std::string address, unsigned short port,
    std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
    std::shared_ptr<UserSessionDirectory> directory, CSession::Submit submit, ContextSource contexts)
    : _io(io), _work(boost::asio::make_work_guard(io)), _strand(boost::asio::make_strand(io)),
      _acceptor(io), _lifecycle(std::move(lifecycle)),
      _directory(std::move(directory)), _submit(std::move(submit)), _contexts(std::move(contexts)),
      _address(std::move(address)), _port(port) {
    if (!_lifecycle || !_directory || !_submit) throw std::invalid_argument("Chat transport dependencies required");
    const auto bind_address = boost::asio::ip::make_address(_address);
    if (!bind_address.is_loopback()) throw std::invalid_argument("Chat transport address must be numeric loopback");
    boost::asio::ip::tcp::endpoint endpoint(bind_address, port);
    _acceptor.open(endpoint.protocol());
#ifdef _WIN32
    _acceptor.set_option(boost::asio::socket_base::reuse_address(false));
#else
    // Preserve Linux restart after active close without allowing duplicate listeners.
    _acceptor.set_option(boost::asio::socket_base::reuse_address(true));
#endif
    _acceptor.bind(endpoint);
    _acceptor.listen();
    _port = _acceptor.local_endpoint().port();
}
bool CServer::Start() {
    if (_stop_requested.load()) return false;
    _ready.store(true);
    boost::asio::post(_strand, [self = shared_from_this()] {
        if (self->_started || self->_stopping) return;
        self->_started = true;
        self->Accept();
    });
    return true;
}
void CServer::Accept() {
    if (_stopping) return;
    auto& io = _contexts ? _contexts() : _io;
    auto session = std::make_shared<CSession>(io, _lifecycle, _directory, _submit);
    _accept_pending = true;
    _acceptor.async_accept(session->Socket(), boost::asio::bind_executor(_strand,
        [self = shared_from_this(), session](boost::system::error_code error) {
            self->_accept_pending = false;
            if (!error && !self->_stopping) {
                self->_sessions.emplace(session->Id(), session);
                self->_connection_count.store(self->_sessions.size());
                session->Start();
            } // Unstarted accepted/failed sockets are released with the temporary session.
            if (!self->_stopping) self->Accept();
            self->CompleteStop();
        }));
}
void CServer::Stop(std::function<void()> completion) {
    _stop_requested.store(true);
    boost::asio::post(_strand, [self = shared_from_this(), completion = std::move(completion)]() mutable {
        if (completion) self->_stop_completions.push_back(std::move(completion));
        if (!self->_stopping) {
            self->_stopping = true;
            self->_ready.store(false);
            boost::system::error_code ignored;
            self->_acceptor.close(ignored);
            for (const auto& entry : self->_sessions) entry.second->Close(SessionCloseReason::ServerShutdown);
        }
        self->CompleteStop();
    });
}
void CServer::RemoveSession(const SessionId& id) {
    boost::asio::post(_strand, [self = shared_from_this(), id] {
        self->_sessions.erase(id);
        self->_connection_count.store(self->_sessions.size());
        self->CompleteStop();
    });
}
void CServer::CompleteStop() {
    if (!_stopping || _accept_pending || !_sessions.empty()) return;
    _stopped.store(true);
    _work.reset();
    auto completions = std::move(_stop_completions);
    _stop_completions.clear();
    for (const auto& completion : completions) completion();
}

} // namespace chat_transport
