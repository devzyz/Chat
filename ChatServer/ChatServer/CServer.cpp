#include "CServer.h"
#include "SessionLifecycleCoordinator.h"

CServer::CServer(boost::asio::io_context& io, unsigned short port,
    std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
    std::shared_ptr<UserSessionDirectory> directory, CSession::Submit submit, ContextSource contexts)
    : _io(io), _strand(boost::asio::make_strand(io)),
      _acceptor(io, {boost::asio::ip::tcp::v4(), port}), _lifecycle(std::move(lifecycle)),
      _directory(std::move(directory)), _submit(std::move(submit)), _contexts(std::move(contexts)),
      _address(_acceptor.local_endpoint().address().to_string()), _port(_acceptor.local_endpoint().port()) {}
void CServer::Start() {
    boost::asio::post(_strand, [self = shared_from_this()] {
        if (self->_started || self->_stopping) return;
        self->_started = true;
        self->_ready.store(true);
        self->Accept();
    });
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
    auto completions = std::move(_stop_completions);
    _stop_completions.clear();
    for (const auto& completion : completions) completion();
}
