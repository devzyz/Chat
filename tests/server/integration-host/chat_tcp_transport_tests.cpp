#include <gtest/gtest.h>

#include "../../../ChatServer/ChatServer/CServer.h"
#include "../../../ChatServer/ChatServer/CSession.h"
#include "../../../ChatServer/ChatServer/ChatFrameCodec.h"
#include "../chat-session-state/session_test_support.h"
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

/** 保存一次接收的帧类型和完整载荷副本。 */
struct ReceivedFrame {
	std::uint16_t id;
	std::string body;
};

/** 线程安全记录分发帧并通知等待者，响应器在锁外执行。 */
class FrameRecorder {
public:
	/** 保存消息副本并唤醒等待者，若设置响应器则在解锁后调用。 */
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

	/** 在锁内替换后续消息使用的响应回调。 */
	void SetResponder(std::function<void(const LogicMessage&)> responder) {
		std::lock_guard<std::mutex> lock(mutex_);
		responder_ = std::move(responder);
	}

	/** 等待接收帧数达到目标或绝对截止时间到达。 */
	bool WaitFor(std::size_t count, std::chrono::steady_clock::time_point deadline) {
		std::unique_lock<std::mutex> lock(mutex_);
		return condition_.wait_until(lock, deadline, /** 检查记录的帧数是否已满足等待目标。 */ [&] { return frames_.size() >= count; });
	}

	/** 在锁内复制全部帧，返回值不借用内部存储。 */
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

/** 拥有真实 Chat 传输链与内存在线状态替身，逐用例管理线程和关闭。 */
class T09_CTCP_Stream : public testing::Test {
protected:
	/** 组装会话目录、生命周期与分发器，启动 loopback Chat 监听。 */
	void SetUp() override {
		directory_ = std::make_shared<UserSessionDirectory>();
        lifecycle_ = std::make_shared<SessionLifecycleCoordinator>(directory_,
            std::make_shared<session_test::MemoryPresence>(), "transport-test");
		dispatcher_ = std::make_shared<LogicDispatcher>(
			/** 记录分发后的逻辑消息并执行测试响应器。 */ [this](const LogicMessage& message) { return recorder_.Record(message); });
		server_ = std::make_shared<chat_transport::CServer>(ioc_, "127.0.0.1", 0, lifecycle_, directory_,
            /** 把传输层收到的消息移交真实分发队列。 */ [this](LogicMessage message) { return dispatcher_->Submit(std::move(message)); });
		lifecycle_->AttachServer(server_);
        ASSERT_TRUE(server_->Start());
		server_thread_ = std::thread(/** 在夹具工作线程运行服务事件循环。 */ [this] { ioc_.run(); });
		ASSERT_TRUE(server_->Ready());
	}

	/** 停止服务并排空生命周期任务，再停止事件线程与分发器。 */
	void TearDown() override {
		if (server_) {
			StopServer();
		}
		lifecycle_->Drain();
		ioc_.stop();
		if (server_thread_.joinable()) {
			server_thread_.join();
		}
		if (dispatcher_) {
			dispatcher_->Stop();
		}
	}

    /** 通过完成通知等待服务停止，已停止时直接返回。 */
    void StopServer() {
        if (server_->Stopped()) return;
        auto done = std::make_shared<std::promise<void>>();
        auto ready = done->get_future();
        server_->Stop(/** 通知等待线程服务停止回调已完成。 */ [done] { done->set_value(); });
        session_test::Await(std::move(ready));
    }

	/** 连接本夹具发布的 Chat TCP 端点。 */
	tcp::socket Connect() {
		tcp::socket socket(client_ioc_);
		socket.connect({boost::asio::ip::make_address(server_->BoundAddress()), server_->BoundPort()});
		return socket;
	}

	/** 将完整测试字节序列写入指定连接。 */
	void Write(tcp::socket& socket, const std::string& bytes) {
		boost::asio::write(socket, boost::asio::buffer(bytes));
	}

	/** 在两秒内等待记录器达到指定帧数。 */
	bool WaitFor(std::size_t count) {
		return recorder_.WaitFor(count, std::chrono::steady_clock::now() + 2s);
	}

	boost::asio::io_context ioc_;
	boost::asio::io_context client_ioc_;
	std::shared_ptr<UserSessionDirectory> directory_;
    std::shared_ptr<SessionLifecycleCoordinator> lifecycle_;
	std::shared_ptr<LogicDispatcher> dispatcher_;
	std::shared_ptr<chat_transport::CServer> server_;
	FrameRecorder recorder_;
	std::thread server_thread_;
};

// T09-CTCP-01
/** 验证服务发布数字 loopback 地址和可连接的端口。 */
TEST_F(T09_CTCP_Stream, PublishesProtocolReadyNumericLoopbackEndpoint) {
	EXPECT_EQ(server_->BoundAddress(), "127.0.0.1");
	EXPECT_NE(server_->BoundPort(), 0);
	auto socket = Connect();
	EXPECT_TRUE(socket.is_open());
}

// T09-CTCP-02
/** 验证分片帧头经生产会话与分发器正确还原。 */
TEST_F(T09_CTCP_Stream, SplitHeaderTraversesProductionSessionAndDispatcher) {
	auto socket = Connect();
	const auto frame = Frame(1201, "header");
	Write(socket, frame.substr(0, 2));
	Write(socket, frame.substr(2));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, "header");
}

// T09-CTCP-03
/** 验证分片帧体经生产会话与分发器正确还原。 */
TEST_F(T09_CTCP_Stream, SplitBodyTraversesProductionSessionAndDispatcher) {
	auto socket = Connect();
	const auto frame = Frame(1202, "split-body");
	Write(socket, frame.substr(0, HEAD_TOTAL_LEN + 3));
	Write(socket, frame.substr(HEAD_TOTAL_LEN + 3));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, "split-body");
}

// T09-CTCP-04
/** 验证粘连相邻帧按原顺序且完整地分发。 */
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
/** 验证零长度载荷只分发一次。 */
TEST_F(T09_CTCP_Stream, ZeroLengthBodyDispatchesExactlyOnce) {
	auto socket = Connect();
	Write(socket, Frame(1205, {}));
	ASSERT_TRUE(WaitFor(1));
	const auto frames = recorder_.Frames();
	ASSERT_EQ(frames.size(), 1u);
	EXPECT_TRUE(frames[0].body.empty());
}

// T09-CTCP-06
/** 验证最大合法载荷未被截断。 */
TEST_F(T09_CTCP_Stream, MaximumLegalBodyDispatchesWithoutTruncation) {
	auto socket = Connect();
	const std::string body(MAX_LENGTH, 'm');
	Write(socket, Frame(1206, body));
	ASSERT_TRUE(WaitFor(1));
	EXPECT_EQ(recorder_.Frames()[0].body, body);
}

// T09-CTCP-07
/** 验证超限一字节的帧在分发前被拒绝，其他连接仍正常。 */
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
/** 验证畸形超长帧头不会使下一连接的帧解析错位。 */
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
/** 验证读取中断仅关闭发生中断的会话。 */
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
/** 验证写入中断期间停止服务仍有界且幂等。 */
TEST_F(T09_CTCP_Stream, StopDuringWriteInterruptionIsBoundedAndIdempotent) {
	auto socket = Connect();
	Write(socket, Frame(1212, std::string(MAX_LENGTH, 'w')));
	boost::system::error_code ignored;
	socket.close(ignored);
	const auto started = std::chrono::steady_clock::now();
	StopServer();
	StopServer();
	EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
	EXPECT_FALSE(server_->Ready());
}

// T09-CTCP-11
/** 验证端口拒连在本用例截止时间内结束。 */
TEST_F(T09_CTCP_Stream, RefusedConnectionCompletesBeforeOwnedDeadline) {
	const auto port = server_->BoundPort();
	StopServer();
	boost::asio::io_context connect_ioc;
	tcp::socket socket(connect_ioc);
	boost::asio::steady_timer deadline(connect_ioc, 250ms);
	boost::system::error_code outcome;
	bool completed = false;
	socket.async_connect({boost::asio::ip::make_address("127.0.0.1"), port},
		/** 保存首次连接结果并取消截止计时器。 */ [&](const boost::system::error_code& error) {
			if (!completed) {
				completed = true;
				outcome = error;
				deadline.cancel();
			}
		});
	deadline.async_wait(/** 连接超时则关闭套接字，保证唯一终态。 */ [&](const boost::system::error_code& error) {
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
/** 验证静默读在所属截止时间到达后取消。 */
TEST_F(T09_CTCP_Stream, SilentReadIsCancelledAtTheOwnedDeadline) {
	auto socket = Connect();
	std::array<char, 1> byte{};
	boost::asio::steady_timer deadline(client_ioc_, 75ms);
	bool timed_out = false;
	bool read_completed = false;
	boost::asio::async_read(socket, boost::asio::buffer(byte),
		/** 标记异步读取已完成并取消超时等待。 */ [&](const boost::system::error_code&, std::size_t) {
			read_completed = true;
			deadline.cancel();
		});
	deadline.async_wait(/** 读取到期时记录超时并关闭连接。 */ [&](const boost::system::error_code& error) {
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
/** 验证排队的大量写入在部分完成下仍按帧完整到达且不重复。 */
TEST_F(T09_CTCP_Stream, QueuedWritesSurvivePartialCompletionsExactlyOnce) {
	constexpr std::size_t reply_count = 512;
	const std::string body(MAX_LENGTH, 'r');
	std::atomic<std::size_t> accepted{0};
	recorder_.SetResponder(/** 收到触发消息后排队发送固定数量的大帧回复。 */ [&](const LogicMessage& message) {
		for (std::size_t index = 0; index < reply_count; ++index) {
			message.session->Send(body, static_cast<std::uint16_t>(2200 + (index % 100)),
                /** 只统计服务确认已入队的回复。 */ [&](SessionSendResult result) { if (result == SessionSendResult::Accepted) ++accepted; });
		}
	});
	auto socket = Connect();
	socket.set_option(boost::asio::socket_base::receive_buffer_size(1024));
	Write(socket, Frame(1213, "write-burst"));
	ASSERT_TRUE(WaitFor(1));
	const auto enqueue_deadline = std::chrono::steady_clock::now() + 1s;
	while (accepted.load() != reply_count && std::chrono::steady_clock::now() < enqueue_deadline) {
		std::this_thread::yield();
	}
	ASSERT_EQ(accepted.load(), reply_count);

	const std::size_t expected_bytes = reply_count * (HEAD_TOTAL_LEN + MAX_LENGTH);
	std::vector<char> received(expected_bytes);
	boost::asio::steady_timer deadline(client_ioc_, 3s);
	std::size_t bytes_read = 0;
	bool timed_out = false;
	boost::asio::async_read(socket, boost::asio::buffer(received),
		/** 记录实际读取字节数并取消截止计时器。 */ [&](const boost::system::error_code&, std::size_t count) {
			bytes_read = count;
			deadline.cancel();
		});
	deadline.async_wait(/** 读取批量回复超时则关闭连接并记录失败原因。 */ [&](const boost::system::error_code& error) {
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
/** 验证重复绑定被拒绝且原监听者继续就绪。 */
TEST_F(T09_CTCP_Stream, OccupiedPortIsRejectedWithoutReplacingTheOwner) {
	auto second_dispatcher = std::make_shared<LogicDispatcher>(/** 为第二个分发器提供无副作用的成功处理器。 */ [](const LogicMessage&) { return true; });
	EXPECT_THROW({
		const auto duplicate = std::make_shared<chat_transport::CServer>(
			ioc_, "127.0.0.1", server_->BoundPort(), lifecycle_, directory_,
            /** 将重复服务接收的消息移交其独立分发器。 */ [second_dispatcher](LogicMessage message) { return second_dispatcher->Submit(std::move(message)); });
		(void)duplicate;
	}, boost::system::system_error);
	EXPECT_TRUE(server_->Ready());
	second_dispatcher->Stop();
}

// T09-CTCP-15
/** 验证停止取消待接受连接并释放端口，允许下一代监听。 */
TEST_F(T09_CTCP_Stream, StopCancelsPendingAcceptAndReleasesThePort) {
	const auto port = server_->BoundPort();
    auto socket = Connect();
    Write(socket, Frame(1214, "rebind"));
    ASSERT_TRUE(WaitFor(1));
	StopServer();
	EXPECT_TRUE(server_->Stopped());
    // Observe the server-initiated close before releasing the peer, leaving the
    // old server generation in TIME_WAIT on Linux.
    boost::asio::steady_timer deadline(client_ioc_, 2s);
    bool closed = false;
    deadline.async_wait(/** 等待对端关闭超时后主动关闭测试套接字。 */ [&](auto error) { if (!error) socket.close(); });
    char byte = 0;
    socket.async_read_some(boost::asio::buffer(&byte, 1), /** 识别服务端 EOF 或 Windows 重置，并结束截止等待。 */ [&](auto error, auto) {
        closed = error == boost::asio::error::eof;
#ifdef _WIN32
        // Winsock close with a pending receive can report a reset instead of FIN.
        closed = closed || error == boost::asio::error::connection_reset;
#endif
        deadline.cancel();
    });
    client_ioc_.run();
    ASSERT_TRUE(closed);
    socket.close();
	boost::asio::io_context probe_ioc;
	tcp::acceptor probe(probe_ioc);
	boost::system::error_code error;
	probe.open(tcp::v4(), error);
	ASSERT_FALSE(error);
#ifndef _WIN32
    probe.set_option(tcp::acceptor::reuse_address(true), error);
    ASSERT_FALSE(error);
#endif
	probe.bind({boost::asio::ip::make_address("127.0.0.1"), port}, error);
	EXPECT_FALSE(error);
}

// T09-CTCP-16
/** 验证停止释放会话、套接字和服务所有权，可重新绑定端口。 */
TEST_F(T09_CTCP_Stream, StopReleasesSessionsThreadsSocketsAndServerOwnership) {
	const auto port = server_->BoundPort();
	auto socket = Connect();
	Write(socket, Frame(1214, "release"));
	ASSERT_TRUE(WaitFor(1));
	boost::system::error_code ignored;
	socket.close(ignored);
	std::weak_ptr<chat_transport::CServer> weak_server = server_;
	StopServer();
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
