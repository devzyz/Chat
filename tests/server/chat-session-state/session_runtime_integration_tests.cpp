#include "session_test_support.h"
using namespace session_test;

// T09-SESSION-01
TEST(SessionRuntimeIntegrationTests, SplitHeaderBodyAndCoalescedFrames) {
    Harness h;
    auto done = std::make_shared<std::promise<std::vector<std::string>>>();
    auto future = done->get_future();
    auto bodies = std::make_shared<std::vector<std::string>>();
    Connected c(h, [done, bodies](LogicMessage message) {
        bodies->push_back(message.body);
        if (bodies->size() == 3) done->set_value(*bodies);
        return LogicSubmitResult::Accepted;
    });
    const auto first = Frame(100, "hello");
    boost::asio::write(c.peer, boost::asio::buffer(first.data(), 1));
    boost::asio::write(c.peer, boost::asio::buffer(first.data() + 1, 5));
    const auto rest = first.substr(6) + Frame(101, "") + Frame(102, std::string(MAX_LENGTH, 'x'));
    boost::asio::write(c.peer, boost::asio::buffer(rest));
    EXPECT_EQ(Await(std::move(future)), (std::vector<std::string>{"hello", "", std::string(MAX_LENGTH, 'x')}));
}
// T09-SESSION-02
TEST(SessionRuntimeIntegrationTests, OversizedHeaderClosesWithoutDispatch) {
    Harness h; std::atomic<int> dispatches{0};
    Connected c(h, [&](LogicMessage) { ++dispatches; return LogicSubmitResult::Accepted; });
    const auto header = ChatFrameCodec::EncodeHeader(100, MAX_LENGTH + 1);
    auto eof = std::make_shared<std::promise<boost::system::error_code>>(); auto future = eof->get_future();
    auto data = std::make_shared<std::array<char, 1>>();
    c.peer.async_read_some(boost::asio::buffer(*data), [data, eof](boost::system::error_code error, std::size_t) {
        eof->set_value(error);
    });
    boost::asio::write(c.peer, boost::asio::buffer(header));
    EXPECT_TRUE(Await(std::move(future)));
    EXPECT_EQ(h.Snapshot(c.session).second, SessionCloseReason::ProtocolError);
    EXPECT_EQ(dispatches, 0);
}
// T09-SESSION-03
TEST(SessionRuntimeIntegrationTests, RepeatedStartKeepsOneReadChain) {
    Harness h; auto result = std::make_shared<std::promise<std::string>>(); auto future = result->get_future();
    Connected c(h, [result](LogicMessage message) { result->set_value(message.body); return LogicSubmitResult::Accepted; });
    c.session->Start(); c.session->Start(); h.Snapshot(c.session);
    boost::asio::write(c.peer, boost::asio::buffer(Frame(100, "one")));
    EXPECT_EQ(Await(std::move(future)), "one");
}
// T09-SESSION-04
TEST(SessionRuntimeIntegrationTests, SendsUseFifoAndAdmissionOnly) {
    Harness h; Connected c(h);
    EXPECT_EQ(h.Send(c.session, {100, "first"}), SessionSendResult::Accepted);
    EXPECT_EQ(h.Send(c.session, {101, "second"}), SessionSendResult::Accepted);
    const auto expected = Frame(100, "first") + Frame(101, "second");
    EXPECT_EQ(c.Read(expected.size()), expected);
}
// T09-SESSION-05
TEST(SessionRuntimeIntegrationTests, ExactQueueCapacityAndCloseDuringWrite) {
    Harness h; Connected c(h);
    auto entered = std::make_shared<std::promise<void>>(); auto entered_future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>(); auto released = release->get_future().share();
    c.session->Inspect([entered, released](SessionState, std::optional<SessionCloseReason>) {
        entered->set_value(); released.wait_for(3s);
    });
    Await(std::move(entered_future));
    std::vector<std::future<SessionSendResult>> results;
    for (int i = 0; i <= MAX_SENDQUE; ++i) {
        auto result = std::make_shared<std::promise<SessionSendResult>>(); results.push_back(result->get_future());
        c.session->Send({100, std::string(MAX_LENGTH, 'x')}, [result](SessionSendResult r) { result->set_value(r); });
    }
    c.session->Close(SessionCloseReason::LocalRequest);
    release->set_value();
    for (int i = 0; i < MAX_SENDQUE; ++i) EXPECT_EQ(Await(std::move(results[i])), SessionSendResult::Accepted);
    EXPECT_EQ(Await(std::move(results.back())), SessionSendResult::Full);
    EXPECT_EQ(h.Snapshot(c.session).first, SessionState::Closing);
    EXPECT_EQ(h.Send(c.session), SessionSendResult::NotActive);
    auto received = std::make_shared<std::promise<std::size_t>>(); auto remaining = received->get_future();
    auto bytes = std::make_shared<std::array<char, MAX_LENGTH * 2 + 8>>();
    boost::asio::async_read(c.peer, boost::asio::buffer(*bytes),
        [received, bytes](boost::system::error_code, std::size_t count) { received->set_value(count); });
    EXPECT_LE(Await(std::move(remaining)), static_cast<std::size_t>(MAX_LENGTH + HEAD_TOTAL_LEN));
}
// T09-SESSION-06
TEST(SessionRuntimeIntegrationTests, AuthenticationReplacementCannotDeleteNewPresence) {
    Harness h; Connected a(h), b(h);
    ASSERT_EQ(h.Bind(a.session), SessionBindResult::Bound);
    ASSERT_EQ(h.Bind(b.session), SessionBindResult::Bound);
    h.lifecycle->CloseReplaced(42, a.session->Id()); h.Snapshot(a.session);
    EXPECT_EQ(h.directory->FindCurrent(42), b.session);
    EXPECT_EQ(h.presence->Find(42).presence->session_id, b.session->Id());
    EXPECT_EQ(a.session->AuthenticatedUid(), 0);
    EXPECT_EQ(h.Bind(b.session, 43), SessionBindResult::AlreadyBound);
}
// T09-SESSION-07
TEST(SessionRuntimeIntegrationTests, CloseDuringPublicationRollsBackAndNeverAuthenticates) {
    Harness h; Connected c(h);
    auto entered = std::make_shared<std::promise<void>>(); auto entering = entered->get_future();
    auto release = std::make_shared<std::promise<void>>(); auto released = release->get_future().share();
    h.presence->before_publish = [entered, released] { entered->set_value(); released.wait_for(3s); };
    auto result = std::make_shared<std::promise<SessionBindResult>>(); auto future = result->get_future();
    c.session->BindAuthenticatedUser(42, [result](SessionBindResult r) { result->set_value(r); });
    Await(std::move(entering));
    h.lifecycle->CloseReplaced(42, c.session->Id());
    EXPECT_EQ(h.Snapshot(c.session).first, SessionState::Closing);
    release->set_value(); EXPECT_EQ(Await(std::move(future)), SessionBindResult::NotActive);
    h.lifecycle->Drain();
    EXPECT_FALSE(h.directory->FindCurrent(42));
    EXPECT_EQ(h.presence->Find(42).status, PresenceStatus::NotFound);
}
// T09-SESSION-08
TEST(SessionRuntimeIntegrationTests, StorageFailureDoesNotPublishLocalIdentity) {
    Harness h; Connected c(h); h.presence->unavailable = true;
    EXPECT_EQ(h.Bind(c.session), SessionBindResult::Unavailable);
    EXPECT_FALSE(h.directory->FindCurrent(42)); EXPECT_EQ(c.session->AuthenticatedUid(), 0);
}
// T09-SESSION-09
TEST(SessionRuntimeIntegrationTests, SlowCleanupDoesNotBlockSocketClose) {
    Harness h; Connected c(h); ASSERT_EQ(h.Bind(c.session), SessionBindResult::Bound);
    auto entered = std::make_shared<std::promise<void>>(); auto future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>(); auto released = release->get_future().share();
    h.presence->before_remove = [entered, released] { entered->set_value(); released.wait_for(3s); };
    c.session->Close(); Await(std::move(future));
    EXPECT_EQ(h.Snapshot(c.session).first, SessionState::Closing);
    EXPECT_FALSE(h.directory->FindCurrent(42));
    EXPECT_EQ(h.Send(c.session), SessionSendResult::NotActive);
    release->set_value();
}
// T09-SESSION-10
TEST(SessionRuntimeIntegrationTests, RemoteReplacementCarriesOldSessionIdentity) {
    auto kicked = std::make_shared<std::promise<chat_session::UserPresence>>(); auto future = kicked->get_future();
    Harness h([kicked](int uid, const chat_session::UserPresence& old) { EXPECT_EQ(uid, 42); kicked->set_value(old); });
    h.presence->Publish(42, {"other-server", "old-session"});
    Connected c(h); ASSERT_EQ(h.Bind(c.session), SessionBindResult::Bound);
    EXPECT_EQ(Await(std::move(future)).session_id, "old-session");
}
// T09-SESSION-11
TEST(SessionRuntimeIntegrationTests, InterruptedReadAndPeerDisconnectReleaseSession) {
    Harness h; auto dispatched = std::make_shared<std::atomic<int>>(0);
    Connected c(h, [dispatched](LogicMessage) { ++*dispatched; return LogicSubmitResult::Accepted; });
    const auto bytes = Frame(100, "partial");
    boost::asio::write(c.peer, boost::asio::buffer(bytes.data(), 6));
    // Explicit close races the pending body completion; queued body cannot restart the read chain.
    c.session->Close(SessionCloseReason::ReadError);
    EXPECT_EQ(h.Snapshot(c.session).second, SessionCloseReason::ReadError);
    EXPECT_EQ(dispatched->load(), 0);
}
// T09-SESSION-12
TEST(SessionRuntimeIntegrationTests, ServerStopDrainsAcceptAndSessionOwnership) {
    Harness h;
    auto received = std::make_shared<std::promise<std::shared_ptr<CSession>>>(); auto incoming = received->get_future();
    auto server = std::make_shared<chat_transport::CServer>(h.io, "127.0.0.1", 0, h.lifecycle, h.directory,
        [received](LogicMessage message) { received->set_value(message.session); return LogicSubmitResult::Accepted; });
    h.lifecycle->AttachServer(server); server->Start();
    boost::asio::ip::tcp::socket peer(h.io);
    peer.connect({boost::asio::ip::address_v4::loopback(), server->BoundPort()});
    boost::asio::write(peer, boost::asio::buffer(Frame(100, "hello")));
    auto session = Await(std::move(incoming)); std::weak_ptr<CSession> weak = session; session.reset();
    auto stopped = std::make_shared<std::promise<void>>(); auto future = stopped->get_future();
    server->Stop([stopped] { stopped->set_value(); }); Await(std::move(future));
    EXPECT_FALSE(server->Ready()); EXPECT_EQ(server->ConnectionCount(), 0U);
    // A final handler can still hold its transient self reference while RemoveSession runs on another strand.
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!weak.expired() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_TRUE(weak.expired());
    boost::asio::ip::tcp::acceptor rebound(h.io, {boost::asio::ip::tcp::v4(), server->BoundPort()});
    auto again = std::make_shared<std::promise<void>>(); auto completed = again->get_future();
    server->Stop([again] { again->set_value(); }); Await(std::move(completed));
}

// T09-SESSION-13
TEST(SessionRuntimeIntegrationTests, WriteFailureClosesAndCleansMatchingPresenceOnce) {
    Harness h;
    auto session = h.Create();
    boost::asio::ip::tcp::acceptor acceptor(h.io, {boost::asio::ip::address_v4::loopback(), 0});
    boost::asio::ip::tcp::socket peer(h.io);
    peer.connect(acceptor.local_endpoint()); acceptor.accept(session->Socket());
    // Establish a deterministic write failure before runtime ownership begins.
    session->Socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send);
    session->Start(); ASSERT_EQ(h.Bind(session), SessionBindResult::Bound);
    auto cleanup = std::make_shared<std::promise<void>>(); auto future = cleanup->get_future();
    h.presence->before_remove = [cleanup] { cleanup->set_value(); };
    EXPECT_EQ(h.Send(session), SessionSendResult::Accepted);
    Await(std::move(future));
    EXPECT_EQ(h.Snapshot(session).second, SessionCloseReason::WriteError);
    EXPECT_FALSE(h.directory->FindCurrent(42));
    session->Close(); h.Snapshot(session); h.lifecycle->Drain();
    EXPECT_EQ(h.presence->removals, 1);
}
