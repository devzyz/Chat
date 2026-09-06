#include <gtest/gtest.h>

#include "../../../ChatServer/ChatServer/CServer.h"
#include "../../../ChatServer/ChatServer/CSession.h"
#include "../../../ChatServer/ChatServer/ChatFrameCodec.h"
#include "../../../ChatServer/ChatServer/ChatSessionStateInternal.h"
#include "../../../ChatServer/ChatServer/Const.h"
#include "../../../ChatServer/ChatServer/LogicDispatcher.h"

#include <boost/asio.hpp>

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
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
		std::function<void(const LogicMessage&)> responder;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			frames_.push_back({static_cast<std::uint16_t>(message.id), message.body});
			responder = responder_;
		}
		condition_.notify_all();
		if (responder) {
			responder(message);
		}
		return true;
	}

	void SetResponder(std::function<void(const LogicMessage&)> responder) {
		std::lock_guard<std::mutex> lock(mutex_);
		responder_ = std::move(responder);
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
	std::function<void(const LogicMessage&)> responder_;
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
		server_ = std::make_shared<chat_transport::CServer>(ioc_, "127.0.0.1", 0, state_, dispatcher_);
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
	std::shared_ptr<chat_transport::CServer> server_;
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
	Write(socket, std::string(reinterpret_cast<const char*>(header.data()), header.size())
		+ std::string(MAX_LENGTH + 1, 'x'));
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

// T09-CTCP-11
TEST_F(T09_CTCP_Stream, RefusedConnectionCompletesBeforeOwnedDeadline) {
	const auto port = server_->BoundPort();
	server_->Stop();
	boost::asio::io_context connect_ioc;
	tcp::socket socket(connect_ioc);
	boost::asio::steady_timer deadline(connect_ioc, 250ms);
	boost::system::error_code outcome;
	bool completed = false;
	socket.async_connect({boost::asio::ip::make_address("127.0.0.1"), port},
		[&](const boost::system::error_code& error) {
			if (!completed) {
				completed = true;
				outcome = error;
				deadline.cancel();
			}
		});
	deadline.async_wait([&](const boost::system::error_code& error) {
		if (!error && !completed) {
			completed = true;
			outcome = boost::asio::error::timed_out;
			boost::system::error_code ignored;
			socket.close(ignored);
		}
	});
	connect_ioc.run();
	EXPECT_TRUE(completed);
	EXPECT_TRUE(outcome);
}

// T09-CTCP-12
TEST_F(T09_CTCP_Stream, SilentReadIsCancelledAtTheOwnedDeadline) {
	auto socket = Connect();
	std::array<char, 1> byte{};
	boost::asio::steady_timer deadline(client_ioc_, 75ms);
	bool timed_out = false;
	bool read_completed = false;
	boost::asio::async_read(socket, boost::asio::buffer(byte),
		[&](const boost::system::error_code&, std::size_t) {
			read_completed = true;
			deadline.cancel();
		});
	deadline.async_wait([&](const boost::system::error_code& error) {
		if (!error) {
			timed_out = true;
			boost::system::error_code ignored;
			socket.close(ignored);
		}
	});
	client_ioc_.restart();
	client_ioc_.run();
	EXPECT_TRUE(timed_out);
	EXPECT_TRUE(read_completed);
}

// T09-CTCP-13
TEST_F(T09_CTCP_Stream, QueuedWritesSurvivePartialCompletionsExactlyOnce) {
	constexpr std::size_t reply_count = 512;
	const std::string body(MAX_LENGTH, 'r');
	std::atomic<std::size_t> accepted{0};
	recorder_.SetResponder([&](const LogicMessage& message) {
		for (std::size_t index = 0; index < reply_count; ++index) {
			if (message.session->Send(body, static_cast<std::uint16_t>(2200 + (index % 100)))
				== SessionSendResult::Accepted) {
				++accepted;
			}
		}
	});
	auto socket = Connect();
	socket.set_option(boost::asio::socket_base::receive_buffer_size(1024));
	Write(socket, Frame(1213, "write-burst"));
	ASSERT_TRUE(WaitFor(1));
	ASSERT_EQ(accepted.load(), reply_count);

	const std::size_t expected_bytes = reply_count * (HEAD_TOTAL_LEN + MAX_LENGTH);
	std::vector<char> received(expected_bytes);
	boost::asio::steady_timer deadline(client_ioc_, 3s);
	std::size_t bytes_read = 0;
	bool timed_out = false;
	boost::asio::async_read(socket, boost::asio::buffer(received),
		[&](const boost::system::error_code&, std::size_t count) {
			bytes_read = count;
			deadline.cancel();
		});
	deadline.async_wait([&](const boost::system::error_code& error) {
		if (!error) {
			timed_out = true;
			boost::system::error_code ignored;
			socket.close(ignored);
		}
	});
	client_ioc_.restart();
	client_ioc_.run();
	EXPECT_FALSE(timed_out);
	EXPECT_EQ(bytes_read, expected_bytes);
}

// T09-CTCP-14
TEST_F(T09_CTCP_Stream, OccupiedPortIsRejectedWithoutReplacingTheOwner) {
	auto second_state = std::make_shared<ChatSessionState>(
		std::make_shared<SequentialSessionIds>(), std::make_shared<InMemoryPresence>());
	auto second_dispatcher = std::make_shared<LogicDispatcher>([](const LogicMessage&) { return true; });
	EXPECT_THROW((std::make_shared<chat_transport::CServer>(
		ioc_, "127.0.0.1", server_->BoundPort(), second_state, second_dispatcher)),
		boost::system::system_error);
	EXPECT_TRUE(server_->Ready());
	second_dispatcher->Stop();
}

// T09-CTCP-15
TEST_F(T09_CTCP_Stream, StopCancelsPendingAcceptAndReleasesThePort) {
	const auto port = server_->BoundPort();
	server_->Stop();
	EXPECT_TRUE(server_->Stopped());
	boost::asio::io_context probe_ioc;
	tcp::acceptor probe(probe_ioc);
	boost::system::error_code error;
	probe.open(tcp::v4(), error);
	ASSERT_FALSE(error);
	probe.bind({boost::asio::ip::make_address("127.0.0.1"), port}, error);
	EXPECT_FALSE(error);
}

// T09-CTCP-16
TEST_F(T09_CTCP_Stream, StopReleasesSessionsThreadsSocketsAndServerOwnership) {
	const auto port = server_->BoundPort();
	auto socket = Connect();
	Write(socket, Frame(1214, "release"));
	ASSERT_TRUE(WaitFor(1));
	boost::system::error_code ignored;
	socket.close(ignored);
	std::weak_ptr<chat_transport::CServer> weak_server = server_;
	server_->Stop();
	EXPECT_TRUE(server_->Stopped());
	server_.reset();
	const auto deadline = std::chrono::steady_clock::now() + 1s;
	while (!weak_server.expired() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}
	EXPECT_TRUE(weak_server.expired());
	boost::asio::io_context probe_ioc;
	tcp::acceptor probe(probe_ioc);
	probe.open(tcp::v4(), ignored);
	ASSERT_FALSE(ignored);
	probe.bind({boost::asio::ip::make_address("127.0.0.1"), port}, ignored);
	EXPECT_FALSE(ignored);
}

} // namespace
