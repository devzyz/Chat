#pragma once
#include "UserPresenceStore.h"
#include "UserSessionDirectory.h"
#include <boost/asio.hpp>
#include <memory>

namespace chat_transport { class CServer; }
class CSession;
class SessionLifecycleCoordinator {
public:
    using Kick = std::function<void(int, const chat_session::UserPresence&)>;
    SessionLifecycleCoordinator(std::shared_ptr<UserSessionDirectory> directory,
        std::shared_ptr<UserPresenceStore> presence, std::string server_id, Kick kick = {});
    ~SessionLifecycleCoordinator();
    void AttachServer(std::weak_ptr<chat_transport::CServer> server);
    void OnAuthenticated(std::shared_ptr<CSession> session, int uid, BindCompletion completion);
    void OnClosing(const SessionId& id, int uid);
    void CloseReplaced(int uid, const SessionId& id);
    void ReleaseOwnership(const SessionId& id);
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
