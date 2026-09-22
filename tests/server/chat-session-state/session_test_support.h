#pragma once
#include "CSession.h"
#include "CServer.h"
#include "SessionLifecycleCoordinator.h"
#include <gtest/gtest.h>
#include <future>
#include <map>
#include <thread>

namespace session_test {
using namespace std::chrono_literals;
template<class T> T Await(std::future<T> future) {
    if (future.wait_for(3s) != std::future_status::ready) throw std::runtime_error("session test timed out");
    return future.get();
}
class MemoryPresence final : public UserPresenceStore {
public:
    PresenceResult Publish(int uid, const chat_session::UserPresence& value) override {
        if (before_publish) before_publish();
        std::lock_guard<std::mutex> lock(mutex);
        if (unavailable) return {};
        auto found = entries.find(uid);
        PresenceResult old = found == entries.end() ? PresenceResult{PresenceStatus::NotFound, {}}
            : PresenceResult{PresenceStatus::Found, found->second};
        entries[uid] = value;
        ++publications;
        return old;
    }
    PresenceResult Find(int uid) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (unavailable) return {};
        auto it = entries.find(uid);
        return it == entries.end() ? PresenceResult{PresenceStatus::NotFound, {}}
            : PresenceResult{PresenceStatus::Found, it->second};
    }
    bool RemoveIfCurrent(int uid, const chat_session::UserPresence& value) override {
        if (before_remove) before_remove();
        std::lock_guard<std::mutex> lock(mutex);
        if (unavailable) return false;
        auto it = entries.find(uid);
        if (it != entries.end() && it->second.server_id == value.server_id && it->second.session_id == value.session_id) {
            entries.erase(it);
            ++removals;
        }
        return true;
    }
    std::function<void()> before_publish;
    std::function<void()> before_remove;
    std::atomic<bool> unavailable{false};
    std::atomic<int> publications{0}, removals{0};
private:
    std::mutex mutex;
    std::map<int, chat_session::UserPresence> entries;
};
struct Harness {
    boost::asio::io_context io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{io.get_executor()};
    std::shared_ptr<UserSessionDirectory> directory = std::make_shared<UserSessionDirectory>();
    std::shared_ptr<MemoryPresence> presence = std::make_shared<MemoryPresence>();
    std::shared_ptr<SessionLifecycleCoordinator> lifecycle;
    std::vector<std::shared_ptr<CSession>> sessions;
    std::thread first, second;
    explicit Harness(SessionLifecycleCoordinator::Kick kick = {})
        : lifecycle(std::make_shared<SessionLifecycleCoordinator>(directory, presence, "test-server", std::move(kick))),
          first([this] { io.run(); }), second([this] { io.run(); }) {}
    ~Harness() {
        for (auto& session : sessions) session->Close();
        for (auto& session : sessions) Snapshot(session);
        lifecycle->Drain();
        guard.reset();
        first.join(); second.join();
    }
    std::shared_ptr<CSession> Create(CSession::Submit submit = [](LogicMessage) { return LogicSubmitResult::Accepted; }) {
        auto session = std::make_shared<CSession>(io, lifecycle, directory, std::move(submit));
        sessions.push_back(session);
        return session;
    }
    std::pair<SessionState, std::optional<SessionCloseReason>> Snapshot(std::shared_ptr<CSession> session) {
        auto promise = std::make_shared<std::promise<std::pair<SessionState, std::optional<SessionCloseReason>>>>();
        auto future = promise->get_future();
        session->Inspect([promise](SessionState state, std::optional<SessionCloseReason> reason) {
            promise->set_value({state, reason});
        });
        return Await(std::move(future));
    }
    SessionBindResult Bind(std::shared_ptr<CSession> session, int uid = 42) {
        auto promise = std::make_shared<std::promise<SessionBindResult>>();
        auto future = promise->get_future();
        session->BindAuthenticatedUser(uid, [promise](SessionBindResult result) { promise->set_value(result); });
        return Await(std::move(future));
    }
    SessionSendResult Send(std::shared_ptr<CSession> session, SessionFrame frame = {100, "body"}) {
        auto promise = std::make_shared<std::promise<SessionSendResult>>();
        auto future = promise->get_future();
        session->Send(std::move(frame), [promise](SessionSendResult result) { promise->set_value(result); });
        return Await(std::move(future));
    }
};
struct Connected {
    Harness& harness;
    std::shared_ptr<CSession> session;
    boost::asio::ip::tcp::socket peer;
    Connected(Harness& h, CSession::Submit submit = [](LogicMessage) { return LogicSubmitResult::Accepted; })
        : harness(h), session(h.Create(std::move(submit))), peer(h.io) {
        boost::asio::ip::tcp::acceptor acceptor(h.io, {boost::asio::ip::address_v4::loopback(), 0});
        peer.connect(acceptor.local_endpoint());
        acceptor.accept(session->Socket());
        session->Start();
        h.Snapshot(session);
    }
    ~Connected() { session->Close(); }
    std::string Read(std::size_t count) {
        auto data = std::make_shared<std::string>(count, '\0');
        auto result = std::make_shared<std::promise<std::string>>();
        auto future = result->get_future();
        boost::asio::async_read(peer, boost::asio::buffer(*data), [data, result](boost::system::error_code error, std::size_t) {
            if (error) result->set_exception(std::make_exception_ptr(std::runtime_error(error.message())));
            else result->set_value(*data);
        });
        return Await(std::move(future));
    }
};
inline std::string Frame(std::uint16_t id, const std::string& body) {
    auto header = ChatFrameCodec::EncodeHeader(id, static_cast<std::uint16_t>(body.size()));
    return std::string(reinterpret_cast<const char*>(header.data()), header.size()) + body;
}
}
