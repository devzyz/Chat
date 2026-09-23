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
/** 在三秒内等待异步结果，超时抛异常，任务异常原样传播。 */
template<class T> T Await(std::future<T> future) {
    if (future.wait_for(3s) != std::future_status::ready) throw std::runtime_error("session test timed out");
    return future.get();
}
/** 以锁保护用户在线归属的内存替身，可注入存储不可用及发布清理屏障。 */
class MemoryPresence final : public UserPresenceStore {
public:
    /** 执行可选发布屏障后替换在线归属并返回旧值，故障时返回不可用。 */
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
    /** 在锁内查询在线归属，区分缺失与不可用。 */
    PresenceResult Find(int uid) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (unavailable) return {};
        auto it = entries.find(uid);
        return it == entries.end() ? PresenceResult{PresenceStatus::NotFound, {}}
            : PresenceResult{PresenceStatus::Found, it->second};
    }
    /** 仅删除仍匹配服务和会话身份的在线归属，执行可选清理屏障。 */
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
/** 拥有双线程 I/O、真实生命周期协调器与内存在线存储，按会话关闭后排空顺序清理。 */
struct Harness {
    boost::asio::io_context io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{io.get_executor()};
    std::shared_ptr<UserSessionDirectory> directory = std::make_shared<UserSessionDirectory>();
    std::shared_ptr<MemoryPresence> presence = std::make_shared<MemoryPresence>();
    std::shared_ptr<SessionLifecycleCoordinator> lifecycle;
    std::vector<std::shared_ptr<CSession>> sessions;
    std::thread first, second;
    /** 组合会话目录和在线存储，启动两条 I/O 工作线程。 */
    explicit Harness(SessionLifecycleCoordinator::Kick kick = {})
        : lifecycle(std::make_shared<SessionLifecycleCoordinator>(directory, presence, "test-server", std::move(kick))),
          first(/** 运行第一条 I/O 消费线程。 */ [this] { io.run(); }), second(/** 运行第二条 I/O 消费线程。 */ [this] { io.run(); }) {}
    /** 关闭并观察所有会话，排空存储协调任务后释放工作守卫并等待线程。 */
    ~Harness() {
        for (auto& session : sessions) session->Close();
        for (auto& session : sessions) Snapshot(session);
        lifecycle->Drain();
        guard.reset();
        first.join(); second.join();
    }
    /** 创建并跟踪共享会话，使用调用方指定的消息提交动作。 */
    std::shared_ptr<CSession> Create(CSession::Submit submit = /** 默认接收入站消息以隔离业务处理因素。 */ [](LogicMessage) { return LogicSubmitResult::Accepted; }) {
        auto session = std::make_shared<CSession>(io, lifecycle, directory, std::move(submit));
        sessions.push_back(session);
        return session;
    }
    /** 在会话执行器读取一致状态并有界等待返回快照。 */
    std::pair<SessionState, std::optional<SessionCloseReason>> Snapshot(std::shared_ptr<CSession> session) {
        auto promise = std::make_shared<std::promise<std::pair<SessionState, std::optional<SessionCloseReason>>>>();
        auto future = promise->get_future();
        session->Inspect(/** 把状态及关闭原因交给快照等待者。 */ [promise](SessionState state, std::optional<SessionCloseReason> reason) {
            promise->set_value({state, reason});
        });
        return Await(std::move(future));
    }
    /** 异步绑定用户并有界等待绑定结果。 */
    SessionBindResult Bind(std::shared_ptr<CSession> session, int uid = 42) {
        auto promise = std::make_shared<std::promise<SessionBindResult>>();
        auto future = promise->get_future();
        session->BindAuthenticatedUser(uid, /** 把绑定结果交给测试等待者。 */ [promise](SessionBindResult result) { promise->set_value(result); });
        return Await(std::move(future));
    }
    /** 异步提交发送帧并有界等待入队结果，不将入队等同送达。 */
    SessionSendResult Send(std::shared_ptr<CSession> session, SessionFrame frame = {100, "body"}) {
        auto promise = std::make_shared<std::promise<SessionSendResult>>();
        auto future = promise->get_future();
        session->Send(std::move(frame), /** 把发送入队结果交给测试等待者。 */ [promise](SessionSendResult result) { promise->set_value(result); });
        return Await(std::move(future));
    }
};
/** 建立真实回环连接并借用所属 Harness，负责关闭该会话。 */
struct Connected {
    Harness& harness;
    std::shared_ptr<CSession> session;
    boost::asio::ip::tcp::socket peer;
    /** 创建会话与对端 socket，完成回环连接后启动会话并等候状态屏障。 */
    Connected(Harness& h, CSession::Submit submit = /** 默认接收入站消息以隔离业务处理因素。 */ [](LogicMessage) { return LogicSubmitResult::Accepted; })
        : harness(h), session(h.Create(std::move(submit))), peer(h.io) {
        boost::asio::ip::tcp::acceptor acceptor(h.io, {boost::asio::ip::address_v4::loopback(), 0});
        peer.connect(acceptor.local_endpoint());
        acceptor.accept(session->Socket());
        session->Start();
        h.Snapshot(session);
    }
    /** 请求关闭本对象关联的会话。 */
    ~Connected() { session->Close(); }
    /** 从回环对端读取指定字节数，有界等待完成或传播读取异常。 */
    std::string Read(std::size_t count) {
        auto data = std::make_shared<std::string>(count, '\0');
        auto result = std::make_shared<std::promise<std::string>>();
        auto future = result->get_future();
        boost::asio::async_read(peer, boost::asio::buffer(*data), /** 持有读取缓冲直到完成，将读取错误或完整字节交给等待者。 */ [data, result](boost::system::error_code error, std::size_t) {
            if (error) result->set_exception(std::make_exception_ptr(std::runtime_error(error.message())));
            else result->set_value(*data);
        });
        return Await(std::move(future));
    }
};
/** 按生产帧头格式拼接消息编号与正文。 */
inline std::string Frame(std::uint16_t id, const std::string& body) {
    auto header = ChatFrameCodec::EncodeHeader(id, static_cast<std::uint16_t>(body.size()));
    return std::string(reinterpret_cast<const char*>(header.data()), header.size()) + body;
}
}
