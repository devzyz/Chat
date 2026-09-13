#include "ChatSessionState.h"
#include "ChatSessionStateInternal.h"
#include "Const.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

TEST(ChatSessionPrincipal, OnlyCurrentLiveOwnedHandleAuthenticates) {
    class Ids final : public SessionIdSource {
        int next = 0;
        std::string Next() override { return std::to_string(++next); }
    };
    class Presence final : public SessionPresence {
        void Register(int, const std::string&) override {}
        void Cleanup(int, const std::string&) override {}
    };
    class Writer final : public SessionWriter {
        void Write(SessionFrame, Completion completion) override { completion(true); }
        void Close() override {}
    };
    ChatSessionState state(std::make_shared<Ids>(), std::make_shared<Presence>());
    ChatSessionState other(std::make_shared<Ids>(), std::make_shared<Presence>());
    auto first = state.Create(std::make_shared<Writer>());
    auto second = state.Create(std::make_shared<Writer>());
    EXPECT_EQ(state.AuthenticatedUid(first), 0);
    state.RegisterCurrent(first, 42);
    EXPECT_EQ(state.AuthenticatedUid(first), 42);
    EXPECT_EQ(other.AuthenticatedUid(first), 0);
    state.RegisterCurrent(second, 42);
    EXPECT_EQ(state.AuthenticatedUid(first), 0);
    EXPECT_EQ(state.AuthenticatedUid(second), 42);
    state.Close(second);
    EXPECT_EQ(state.AuthenticatedUid(second), 0);
    EXPECT_THROW(state.RegisterCurrent(second, 42), std::invalid_argument);
}

class FixedIdSource final : public SessionIdSource {
public:
	explicit FixedIdSource(std::deque<std::string> ids) : ids_(std::move(ids)) {}
	std::string Next() override {
		if (ids_.empty()) {
			return {};
		}
		auto id = std::move(ids_.front());
		ids_.pop_front();
		return id;
	}
private:
	std::deque<std::string> ids_;
};

class RecordingPresence final : public SessionPresence {
public:
	void Register(int uid, const std::string& session_id) override {
		std::lock_guard<std::mutex> lock(mutex_);
		registrations_.emplace_back(uid, session_id);
	}
	void Cleanup(int uid, const std::string& session_id) override {
		std::lock_guard<std::mutex> lock(mutex_);
		cleanups_.emplace_back(uid, session_id);
	}
	std::size_t CleanupCount() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return cleanups_.size();
	}
private:
	mutable std::mutex mutex_;
	std::vector<std::pair<int, std::string>> registrations_;
	std::vector<std::pair<int, std::string>> cleanups_;
};

class ManualWriter final : public SessionWriter {
public:
	void Write(SessionFrame frame, Completion completion) override {
		std::lock_guard<std::mutex> lock(mutex_);
		started_.push_back(frame);
		pending_.emplace_back(std::move(frame), std::move(completion));
	}
	void Close() override {
		std::lock_guard<std::mutex> lock(mutex_);
		++close_count_;
	}
	std::size_t CloseCount() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return close_count_;
	}
	std::vector<SessionFrame> Started() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return started_;
	}
	std::size_t PendingCount() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return pending_.size();
	}
	void CompleteNext(bool success, bool duplicate_callback = false) {
		Completion completion;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (pending_.empty()) {
				return;
			}
			completion = std::move(pending_.front().second);
			pending_.pop_front();
		}
		completion(success);
		if (duplicate_callback) {
			completion(success);
		}
	}
private:
	mutable std::mutex mutex_;
	std::vector<SessionFrame> started_;
	std::deque<std::pair<SessionFrame, Completion>> pending_;
	std::size_t close_count_ = 0;
};

class StartBarrier {
public:
	explicit StartBarrier(std::size_t expected) : expected_(expected) {}
	void ArriveAndWait() {
		std::unique_lock<std::mutex> lock(mutex_);
		++arrived_;
		ready_.notify_one();
		release_.wait(lock, [this] { return released_; });
	}
	bool WaitUntilReady(std::chrono::milliseconds timeout) {
		std::unique_lock<std::mutex> lock(mutex_);
		return ready_.wait_for(lock, timeout, [this] { return arrived_ == expected_; });
	}
	void Release() {
		std::lock_guard<std::mutex> lock(mutex_);
		released_ = true;
		release_.notify_all();
	}
private:
	std::mutex mutex_;
	std::condition_variable ready_;
	std::condition_variable release_;
	std::size_t expected_;
	std::size_t arrived_ = 0;
	bool released_ = false;
};

// T08-SESSION-01
TEST(ChatSessionStateTests, NewSessionHandlesAreNonEmptyAndUnique) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-a", "session-b"});
	auto presence = std::make_shared<RecordingPresence>();
	ChatSessionState state(ids, presence);

	auto first = state.Create(std::make_shared<ManualWriter>());
	auto second = state.Create(std::make_shared<ManualWriter>());

	EXPECT_TRUE(first);
	EXPECT_TRUE(second);
	EXPECT_NE(first, second);
}

// T08-SESSION-02
TEST(ChatSessionStateTests, FirstRegistrationBecomesTheCurrentSession) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-current"});
	auto presence = std::make_shared<RecordingPresence>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(std::make_shared<ManualWriter>());

	state.RegisterCurrent(session, 42);

	EXPECT_EQ(state.FindCurrent(42), session);
}

// T08-SESSION-03
TEST(ChatSessionStateTests, ANewSessionAtomicallyReplacesTheCurrentSession) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-old", "session-new"});
	auto presence = std::make_shared<RecordingPresence>();
	ChatSessionState state(ids, presence);
	auto old_session = state.Create(std::make_shared<ManualWriter>());
	auto new_session = state.Create(std::make_shared<ManualWriter>());

	state.RegisterCurrent(old_session, 42);
	state.RegisterCurrent(new_session, 42);

	EXPECT_EQ(state.FindCurrent(42), new_session);
	EXPECT_NE(state.FindCurrent(42), old_session);
}

// T08-SESSION-04
TEST(ChatSessionStateTests, ClosingTheReplacedSessionDoesNotDeleteTheNewMapping) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-old", "session-new"});
	auto presence = std::make_shared<RecordingPresence>();
	ChatSessionState state(ids, presence);
	auto old_session = state.Create(std::make_shared<ManualWriter>());
	auto new_session = state.Create(std::make_shared<ManualWriter>());
	state.RegisterCurrent(old_session, 42);
	state.RegisterCurrent(new_session, 42);

	state.Close(old_session);

	EXPECT_EQ(state.FindCurrent(42), new_session);
	EXPECT_EQ(presence->CleanupCount(), 0U);
}

// T08-SESSION-05
TEST(ChatSessionStateTests, ClosingTheCurrentSessionCleansUpOnceAndIsIdempotent) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-current"});
	auto presence = std::make_shared<RecordingPresence>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(std::make_shared<ManualWriter>());
	state.RegisterCurrent(session, 42);

	state.Close(session);
	state.Close(session);

	EXPECT_FALSE(state.FindCurrent(42));
	EXPECT_EQ(presence->CleanupCount(), 1U);
}

// T08-SESSION-06
TEST(ChatSessionStateTests, ConcurrentClosePerformsPresenceCleanupAtMostOnce) {
	constexpr std::size_t closer_count = 8;
	constexpr auto timeout = std::chrono::seconds(2);
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-current"});
	auto presence = std::make_shared<RecordingPresence>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(std::make_shared<ManualWriter>());
	state.RegisterCurrent(session, 42);
	StartBarrier barrier(closer_count);
	std::vector<std::thread> closers;
	std::vector<std::future<void>> completed;
	for (std::size_t index = 0; index < closer_count; ++index) {
		std::promise<void> done;
		completed.push_back(done.get_future());
		closers.emplace_back([&state, session, &barrier, done = std::move(done)]() mutable {
			barrier.ArriveAndWait();
			state.Close(session);
			done.set_value();
		});
	}
	const bool all_ready = barrier.WaitUntilReady(timeout);
	barrier.Release();
	for (auto& done : completed) {
		EXPECT_EQ(done.wait_for(timeout), std::future_status::ready);
	}
	for (auto& closer : closers) {
		closer.join();
	}

	EXPECT_TRUE(all_ready);
	EXPECT_FALSE(state.FindCurrent(42));
	EXPECT_EQ(presence->CleanupCount(), 1U);
}

// T08-SESSION-07
TEST(ChatSessionStateTests, AcceptedFramesAreWrittenInFifoOrder) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-writer"});
	auto presence = std::make_shared<RecordingPresence>();
	auto writer = std::make_shared<ManualWriter>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(writer);

	EXPECT_EQ(state.Send(session, {1, "first"}), SessionSendResult::Accepted);
	EXPECT_EQ(state.Send(session, {2, "second"}), SessionSendResult::Accepted);
	EXPECT_EQ(state.Send(session, {3, "third"}), SessionSendResult::Accepted);
	ASSERT_EQ(writer->Started().size(), 1U);
	EXPECT_EQ(writer->Started()[0].body, "first");

	writer->CompleteNext(true);
	ASSERT_EQ(writer->Started().size(), 2U);
	EXPECT_EQ(writer->Started()[1].body, "second");
	writer->CompleteNext(true);
	ASSERT_EQ(writer->Started().size(), 3U);
	EXPECT_EQ(writer->Started()[2].body, "third");
	writer->CompleteNext(true);

	EXPECT_EQ(writer->PendingCount(), 0U);
	state.Close(session);
}

// T08-SESSION-08
TEST(ChatSessionStateTests, ExactCapacityRejectsOnlyTheNextFrameWithoutOverwriting) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-capacity"});
	auto presence = std::make_shared<RecordingPresence>();
	auto writer = std::make_shared<ManualWriter>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(writer);

	for (std::size_t index = 0; index < MAX_SENDQUE; ++index) {
		EXPECT_EQ(state.Send(session, {7, "accepted-" + std::to_string(index)}),
			SessionSendResult::Accepted);
	}
	EXPECT_EQ(state.Send(session, {7, "must-not-overwrite"}), SessionSendResult::Full);

	for (std::size_t index = 0; index < MAX_SENDQUE; ++index) {
		writer->CompleteNext(true);
	}
	const auto started = writer->Started();
	ASSERT_EQ(started.size(), static_cast<std::size_t>(MAX_SENDQUE));
	EXPECT_EQ(started.front().body, "accepted-0");
	EXPECT_EQ(started.back().body, "accepted-999");
	EXPECT_EQ(writer->PendingCount(), 0U);
	state.Close(session);
}

// T08-SESSION-09
TEST(ChatSessionStateTests, ClosedSessionRejectsNewFramesImmediately) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-closed"});
	auto presence = std::make_shared<RecordingPresence>();
	auto writer = std::make_shared<ManualWriter>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(writer);
	EXPECT_EQ(state.Send(session, {1, "in-flight"}), SessionSendResult::Accepted);

	state.Close(session);

	EXPECT_EQ(state.Send(session, {2, "rejected"}), SessionSendResult::Closed);
	EXPECT_EQ(writer->CloseCount(), 1U);
	EXPECT_EQ(writer->Started().size(), 1U);
	writer->CompleteNext(true);
	EXPECT_EQ(writer->PendingCount(), 0U);
}

// T08-SESSION-10
TEST(ChatSessionStateTests, WriterFailureClosesAndCleansUpExactlyOnce) {
	auto ids = std::make_shared<FixedIdSource>(
		std::deque<std::string>{"session-failure"});
	auto presence = std::make_shared<RecordingPresence>();
	auto writer = std::make_shared<ManualWriter>();
	ChatSessionState state(ids, presence);
	auto session = state.Create(writer);
	state.RegisterCurrent(session, 42);
	EXPECT_EQ(state.Send(session, {1, "will-fail"}), SessionSendResult::Accepted);

	writer->CompleteNext(false, true);

	EXPECT_FALSE(state.FindCurrent(42));
	EXPECT_EQ(presence->CleanupCount(), 1U);
	EXPECT_EQ(writer->CloseCount(), 1U);
	EXPECT_EQ(state.Send(session, {2, "after-failure"}), SessionSendResult::Closed);
	EXPECT_EQ(writer->PendingCount(), 0U);
}

} // namespace
