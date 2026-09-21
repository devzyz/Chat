#include "SessionLifecycleCoordinator.h"
#include "CServer.h"
#include "CSession.h"
#include <future>
#include <spdlog/spdlog.h>

SessionLifecycleCoordinator::SessionLifecycleCoordinator(std::shared_ptr<UserSessionDirectory> directory,
    std::shared_ptr<UserPresenceStore> presence, std::string server_id, Kick kick)
    : _directory(std::move(directory)), _presence(std::move(presence)),
      _server_id(std::move(server_id)), _kick(std::move(kick)) {}
SessionLifecycleCoordinator::~SessionLifecycleCoordinator() { _worker.join(); }
void SessionLifecycleCoordinator::AttachServer(std::weak_ptr<CServer> server) { _server = std::move(server); }
void SessionLifecycleCoordinator::Drain() { _worker.join(); }
void SessionLifecycleCoordinator::OnAuthenticated(std::shared_ptr<CSession> session, int uid,
    BindCompletion completion) {
    {
        std::lock_guard<std::mutex> lock(_binding_mutex);
        _binding_sessions.emplace(session->Id(), std::make_pair(uid, session));
    }
    boost::asio::post(_worker, [this, session, uid, completion = std::move(completion)]() mutable {
        const chat_session::UserPresence current{_server_id, session->Id()};
        PresenceResult result;
        try { result = _presence->Publish(uid, current); }
        catch (const std::exception& error) { SPDLOG_ERROR("presence publish failed: {}", error.what()); }
        auto committed = std::make_shared<std::promise<bool>>();
        auto ready = committed->get_future();
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        session->FinishBinding(uid, result.status == PresenceStatus::Unavailable
            ? SessionBindResult::Unavailable : SessionBindResult::Bound, std::move(completion), cancelled,
            [committed](bool success) { committed->set_value(success); });
        // Only the dedicated lifecycle worker waits; no socket executor or business worker is blocked.
        const bool completed = ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        const bool success = completed && ready.get();
        {
            std::lock_guard<std::mutex> lock(_binding_mutex);
            _binding_sessions.erase(session->Id());
        }
        if (!success) {
            cancelled->store(true);
            session->Close(SessionCloseReason::LocalRequest);
            try {
                if (!_presence->RemoveIfCurrent(uid, current)) SPDLOG_WARN("presence rollback failed, uid={}", uid);
            } catch (const std::exception& error) { SPDLOG_WARN("presence rollback failed: {}", error.what()); }
        }
        // Publication has replaced the old global owner even if this connection disconnected meanwhile.
        if (result.presence && result.presence->session_id != current.session_id) {
            if (result.presence->server_id == _server_id) {
                auto old = _directory->FindCurrent(uid);
                if (old && old->Id() == result.presence->session_id) old->Close(SessionCloseReason::Replaced);
            } else if (_kick) {
                try { _kick(uid, *result.presence); }
                catch (const std::exception& error) { SPDLOG_WARN("remote replacement failed: {}", error.what()); }
            }
        }
    });
}
void SessionLifecycleCoordinator::CloseReplaced(int uid, const SessionId& id) {
    if (id.empty()) return; // Legacy UID-only kicks cannot safely identify a connection.
    std::shared_ptr<CSession> session;
    {
        std::lock_guard<std::mutex> lock(_binding_mutex);
        const auto it = _binding_sessions.find(id);
        if (it != _binding_sessions.end() && it->second.first == uid) session = it->second.second.lock();
    }
    if (!session) session = _directory->FindCurrent(uid);
    if (session && session->Id() == id) session->Close(SessionCloseReason::Replaced);
}
void SessionLifecycleCoordinator::OnClosing(const SessionId& id, int uid) {
    if (uid <= 0) return;
    _directory->UnregisterIfCurrent(uid, id);
    boost::asio::post(_worker, [this, id, uid] {
        try {
            if (!_presence->RemoveIfCurrent(uid, {_server_id, id}))
                SPDLOG_WARN("presence cleanup failed, uid={}", uid);
        } catch (const std::exception& error) { SPDLOG_WARN("presence cleanup failed: {}", error.what()); }
    });
}
void SessionLifecycleCoordinator::ReleaseOwnership(const SessionId& id) {
    if (auto server = _server.lock()) server->RemoveSession(id);
}
