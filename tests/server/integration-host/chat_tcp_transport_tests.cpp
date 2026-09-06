#include <gtest/gtest.h>

#include "../../../ChatServer/ChatServer/CServer.h"
#include "../../../ChatServer/ChatServer/ChatFrameCodec.h"
#include "../../../ChatServer/ChatServer/ChatSessionStateInternal.h"
#include "../../../ChatServer/ChatServer/Const.h"
#include "../../../ChatServer/ChatServer/LogicDispatcher.h"

#include <boost/asio.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using tcp = boost::asio::ip::tcp;

class SequentialSessionIds final : public SessionIdSource {
public:
	std::string Next() override { return "chat-loopback-" + std::to_string(++next_); }
private:
	std::uint64_t next_ = 0;
};

class InMemoryPresence final : public SessionPresence {
public:
	void Register(int, const std::string&) override {}
	void Cleanup(int, const std::string&) override {}
};

struct ReceivedFrame {
	std::uint16_t id;
	std::string body;
};

class FrameRecorder {
public:
	bool Record(const LogicMessage& message) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			frames_.push_back({static_cast<std::uint16_t>(message.id), message.body});
		}
		condition_.notify_all();
		return true;
	}

	bool WaitFor(std::size_t count, std::chrono::steady_clock::time_point deadline) {
		std::unique_lock<std::mutex> lock(mutex_);
		return condition_.wait_until(lock, deadline, [&] { return frames_.size() >= count; });
	}

	std::vector<ReceivedFrame> Frames() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return frames_;
	}

private:
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	std::vector<ReceivedFrame> frames_;
};

std::string Frame(std::uint16_t id, const std::string& body) {
	const auto header = ChatFrameCodec::EncodeHeader(id, static_cast<std::uint16_t>(body.size()));
	return std::string(reinterpret_cast<const char*>(header.data()), header.size()) + body;
}

class T09_CTCP_Stream : public testing::Test {
protected:
	void SetUp() override {
		state_ = std::make_shared<ChatSessionState>(
			std::make_shared<SequentialSessionIds>(), std::make_shared<InMemoryPresence>());
		dispatcher_ = std::make_shared<LogicDispatcher>(
			[this](const LogicMessage& message) { return recorder_.Record(message); });
		server_ = std::make_shared<CServer>(ioc_, "127.0.0.1", 0, state_, dispatcher_);
		ASSERT_TRUE(server_->Start());
		server_thread_ = std::thread([this] { ioc_.run(); });
		ASSERT_TRUE(server_->Ready());
	}

	void TearDown() override {
		if (server_) {
			server_->Stop();
		}
		ioc_.stop();
		if (server_thread_.joinable()) {
			server_thread_.join();
		}
		if (dispatcher_) {
			dispatcher_->Stop();
		}
	}

	tcp::socket Connect() {
		tcp::socket socket(client_ioc_);
		socket.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});
		return socket;
	}

	void Write(tcp::socket& socket, const std::string& bytes) {
		boost::asio::write(socket, boost::asio::buffer(bytes));
	}

	bool WaitFor(std::size_t count) {
		return recorder_.WaitFor(count, std::chrono::steady_clock::now() + 2s);
	}

	boost::asio::io_context ioc_;
	boost::asio::io_context client_ioc_;
	std::shared_ptr<ChatSessionState> state_;
	std::shared_ptr<LogicDispatcher> dispatcher_;
	std::shared_ptr<CServer> server_;
	FrameRecorder recorder_;
	std::thread server_thread_;
};

// T09-CTCP-01
TEST_F(T09_CTCP_Stream, PublishesProtocolReadyNumericLoopbackEndpoint) {
	EXPECT_EQ(server_->BoundAddress(), "127.0.0.1");
	EXPECT_NE(server_->BoundPort(), 0);
	auto socket = Connect();
	EXPECT_TRUE(socket.is_open());
}

// T09-CTCP-02
TEST_F(T09_CTCP_Stream, SplitHeaderTraversesProductionSessionAndDispatcher) {
	auto socket = Connect();
	const auto frame = Frame(1201, "header");
	Write(socket, frame.substr(0, 2));
	Write(socket, frame.substr(2));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, "header");
}

// T09-CTCP-03
TEST_F(T09_CTCP_Stream, SplitBodyTraversesProductionSessionAndDispatcher) {
	auto socket = Connect();
	const auto frame = Frame(1202, "split-body");
	Write(socket, frame.substr(0, HEAD_TOTAL_LEN + 3));
	Write(socket, frame.substr(HEAD_TOTAL_LEN + 3));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, "split-body");
}

// T09-CTCP-04
TEST_F(T09_CTCP_Stream, CoalescedAdjacentFramesDispatchInExactOrder) {
	auto socket = Connect();
	Write(socket, Frame(1203, "first") + Frame(1204, "second"));
	ASSERT_TRUE(WaitFor(2));
	const auto frames = recorder_.Frames();
	ASSERT_EQ(frames.size(), 2u);
	EXPECT_EQ(frames[0].id, 1203);
	EXPECT_EQ(frames[0].body, "first");
	EXPECT_EQ(frames[1].id, 1204);
	EXPECT_EQ(frames[1].body, "second");
}

// T09-CTCP-05
TEST_F(T09_CTCP_Stream, ZeroLengthBodyDispatchesExactlyOnce) {
	auto socket = Connect();
	Write(socket, Frame(1205, {}));
	ASSERT_TRUE(WaitFor(1));
	const auto frames = recorder_.Frames();
	ASSERT_EQ(frames.size(), 1u);
	EXPECT_TRUE(frames[0].body.empty());
}

// T09-CTCP-06
TEST_F(T09_CTCP_Stream, MaximumLegalBodyDispatchesWithoutTruncation) {
	auto socket = Connect();
	const std::string body(MAX_LENGTH, 'm');
	Write(socket, Frame(1206, body));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, body);
}

// T09-CTCP-07
TEST_F(T09_CTCP_Stream, OneByteOverMaximumClosesBeforeDispatch) {
	auto socket = Connect();
	const auto header = ChatFrameCodec::EncodeHeader(1207, MAX_LENGTH + 1);
	boost::asio::write(socket, boost::asio::buffer(header));
	EXPECT_FALSE(recorder_.WaitFor(1, std::chrono::steady_clock::now() + 150ms));
	auto healthy = Connect();
	Write(healthy, Frame(1208, "healthy"));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, "healthy");
}

// T09-CTCP-08
TEST_F(T09_CTCP_Stream, MalformedOversizedHeaderCannotDesynchronizeNextConnection) {
	auto socket = Connect();
	const std::string malformed("\x04\xb9\xff\xff", 4);
	Write(socket, malformed);
	boost::system::error_code ignored;
	socket.close(ignored);
	auto next = Connect();
	Write(next, Frame(1209, "next-generation"));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].id, 1209);
}

// T09-CTCP-09
TEST_F(T09_CTCP_Stream, ReadInterruptionClosesOnlyTheInterruptedSession) {
	auto interrupted = Connect();
	Write(interrupted, Frame(1210, "partial").substr(0, HEAD_TOTAL_LEN + 2));
	boost::system::error_code ignored;
	interrupted.close(ignored);
	auto healthy = Connect();
	Write(healthy, Frame(1211, "after-interrupt"));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, "after-interrupt");
}

// T09-CTCP-10
TEST_F(T09_CTCP_Stream, StopDuringWriteInterruptionIsBoundedAndIdempotent) {
	auto socket = Connect();
	Write(socket, Frame(1212, std::string(MAX_LENGTH, 'w')));
	boost::system::error_code ignored;
	socket.close(ignored);
	const auto started = std::chrono::steady_clock::now();
	server_->Stop();
	server_->Stop();
	EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
	EXPECT_FALSE(server_->Ready());
}

} // namespace
