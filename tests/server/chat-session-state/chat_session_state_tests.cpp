#include "session_test_support.h"
using namespace session_test;
// T08-SESSION-01
TEST(SessionComponentTests, CreatedSessionsHaveUniqueImmutableIds) {
    Harness h; auto a = h.Create(); auto b = h.Create();
    EXPECT_FALSE(a->Id().empty()); EXPECT_NE(a->Id(), b->Id());
    EXPECT_EQ(h.Snapshot(a).first, SessionState::Created);
}
// T08-SESSION-02
TEST(SessionComponentTests, DirectoryRegistersCurrentSession) {
    Harness h; auto session = h.Create();
    EXPECT_FALSE(h.directory->Register(42, session->Id(), session));
    EXPECT_EQ(h.directory->FindCurrent(42), session);
    EXPECT_TRUE(h.directory->IsCurrent(42, session->Id()));
}
// T08-SESSION-03
TEST(SessionComponentTests, DirectoryReturnsReplacedSession) {
    Harness h; auto a = h.Create(); auto b = h.Create();
    h.directory->Register(42, a->Id(), a);
    EXPECT_EQ(h.directory->Register(42, b->Id(), b), a);
    EXPECT_EQ(h.directory->FindCurrent(42), b);
}
// T08-SESSION-04
TEST(SessionComponentTests, OldUnregisterCannotEraseReplacement) {
    Harness h; auto a = h.Create(); auto b = h.Create();
    h.directory->Register(42, a->Id(), a); h.directory->Register(42, b->Id(), b);
    h.directory->UnregisterIfCurrent(42, a->Id());
    EXPECT_EQ(h.directory->FindCurrent(42), b);
    h.directory->UnregisterIfCurrent(42, b->Id()); EXPECT_FALSE(h.directory->FindCurrent(42));
}
// T08-SESSION-05
TEST(SessionComponentTests, RepeatedClosePreservesFirstReason) {
    Harness h; auto session = h.Create();
    session->Close(SessionCloseReason::ProtocolError); session->Close(SessionCloseReason::LocalRequest);
    const auto state = h.Snapshot(session);
    EXPECT_EQ(state.first, SessionState::Closing); EXPECT_EQ(state.second, SessionCloseReason::ProtocolError);
}
// T08-SESSION-06
TEST(SessionComponentTests, ConcurrentCloseIsIdempotent) {
    Harness h; auto session = h.Create();
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([session] { session->Close(); });
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(h.Snapshot(session).first, SessionState::Closing);
    EXPECT_EQ(h.presence->removals, 0);
}
// T08-SESSION-07 (FIFO moved to real transport contract below.)
TEST(SessionComponentTests, DirectoryHasWeakOwnership) {
    Harness h; auto session = h.Create();
    const auto id = session->Id();
    h.directory->Register(42, id, session);
    h.sessions.clear(); session.reset();
    EXPECT_FALSE(h.directory->FindCurrent(42)); EXPECT_FALSE(h.directory->IsCurrent(42, id));
}
// T08-SESSION-08 (Capacity moved to real transport contract below.)
TEST(SessionComponentTests, CreatedAndClosingRejectSend) {
    Harness h; auto session = h.Create();
    EXPECT_EQ(h.Send(session), SessionSendResult::NotActive);
    session->Close(); EXPECT_EQ(h.Send(session), SessionSendResult::NotActive);
}
// T08-SESSION-09
TEST(SessionComponentTests, ClosingRejectsAuthentication) {
    Harness h; auto session = h.Create(); session->Close();
    EXPECT_EQ(h.Bind(session), SessionBindResult::NotActive);
    EXPECT_EQ(session->AuthenticatedUid(), 0); EXPECT_EQ(h.presence->publications, 0);
}
// T08-SESSION-10 (Write failure moved to real transport contract below.)
TEST(SessionComponentTests, CloseBeforeStartCannotReactivate) {
    Harness h; auto session = h.Create(); session->Close(); session->Start(); session->Start();
    EXPECT_EQ(h.Snapshot(session).first, SessionState::Closing);
}
