#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "LogicDispatcher.h"
#include "Const.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

/** 作用域内捕获默认日志输出，用于验证正文和异常不泄露。 */
class ScopedLogCapture {
public:
	/** 安装内存日志器并保存原默认日志器。 */
	ScopedLogCapture()
		: previous_(spdlog::default_logger()),
		  sink_(std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_)),
		  logger_(std::make_shared<spdlog::logger>("logic-dispatcher-test", sink_)) {
		logger_->set_pattern("%v");
		logger_->set_level(spdlog::level::trace);
		spdlog::set_default_logger(logger_);
	}

	/** 恢复原默认日志器。 */
	~ScopedLogCapture() {
		spdlog::set_default_logger(previous_);
	}

	/** 刷新并返回捕获日志文本。 */
	std::string Text() {
		logger_->flush();
		return stream_.str();
	}

private:
	std::shared_ptr<spdlog::logger> previous_;
	std::ostringstream stream_;
	std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink_;
	std::shared_ptr<spdlog::logger> logger_;
};

// T08-LOGIC-01
/** 验证被接受消息按 FIFO 顺序委派。 */
TEST(LogicDispatcherTests, AcceptedMessagesDispatchInFifoOrder) {
	std::mutex mutex;
	std::condition_variable dispatched;
	std::vector<std::int16_t> ids;
	LogicDispatcher dispatcher(/** 记录消息编号并唤醒等待者。 */ [&](const LogicMessage& message) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			ids.push_back(message.id);
		}
		dispatched.notify_all();
		return true;
	});

	EXPECT_EQ(dispatcher.Submit({nullptr, 11, "first"}), LogicSubmitResult::Accepted);
	EXPECT_EQ(dispatcher.Submit({nullptr, 12, "second"}), LogicSubmitResult::Accepted);
	EXPECT_EQ(dispatcher.Submit({nullptr, 13, "third"}), LogicSubmitResult::Accepted);

	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(dispatched.wait_for(lock, 2s, /** 判断三条消息是否全部处理。 */ [&]() { return ids.size() == 3; }));
	}
	dispatcher.Stop();
	EXPECT_EQ(ids, (std::vector<std::int16_t>{11, 12, 13}));
}

// T08-LOGIC-02
/** 验证并发生产者的每条已接受消息恰好处理一次。 */
TEST(LogicDispatcherTests, ConcurrentProducersDispatchEveryAcceptedMessageExactlyOnce) {
	constexpr int producer_count = 8;
	constexpr int messages_per_producer = 50;
	constexpr int message_count = producer_count * messages_per_producer;
	std::mutex mutex;
	std::condition_variable condition;
	bool start = false;
	std::vector<int> dispatch_counts(message_count, 0);
	int dispatched_count = 0;
	std::vector<LogicSubmitResult> results(message_count, LogicSubmitResult::Closed);
	LogicDispatcher dispatcher(/** 在锁内累计各消息处理次数并通知等待者。 */ [&](const LogicMessage& message) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			++dispatch_counts[static_cast<std::size_t>(std::stoi(message.body))];
			++dispatched_count;
		}
		condition.notify_all();
		return true;
	});

	std::vector<std::thread> producers;
	for (int producer = 0; producer < producer_count; ++producer) {
		producers.emplace_back(/** 等待起跑屏障后提交本生产者的独立消息区间。 */ [&, producer]() {
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, /** 检查并发生产者起跑屏障。 */ [&]() { return start; });
			}
			for (int index = 0; index < messages_per_producer; ++index) {
				const int value = producer * messages_per_producer + index;
				results[static_cast<std::size_t>(value)] =
					dispatcher.Submit({nullptr, static_cast<std::int16_t>(value), std::to_string(value)});
			}
		});
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		start = true;
	}
	condition.notify_all();
	for (auto& producer : producers) {
		producer.join();
	}

	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 2s, /** 判断全部消息是否处理完成。 */ [&]() { return dispatched_count == message_count; }));
	}
	dispatcher.Stop();
	EXPECT_EQ(std::count(results.begin(), results.end(), LogicSubmitResult::Accepted), message_count);
	EXPECT_TRUE(std::all_of(dispatch_counts.begin(), dispatch_counts.end(), /** 检查单条消息恰好处理一次。 */ [](int count) { return count == 1; }));
}

// T08-LOGIC-03
/** 验证待处理队列恰好满时仅拒绝下一条消息。 */
TEST(LogicDispatcherTests, ExactPendingCapacityRejectsOnlyTheNextMessage) {
	std::mutex mutex;
	std::condition_variable condition;
	bool first_handler_started = false;
	bool release_handler = false;
	std::vector<int> dispatched;
	LogicDispatcher dispatcher(/** 记录委派消息并阻塞首条处理以构造满队列。 */ [&](const LogicMessage& message) {
		std::unique_lock<std::mutex> lock(mutex);
		dispatched.push_back(std::stoi(message.body));
		if (!first_handler_started) {
			first_handler_started = true;
			condition.notify_all();
			condition.wait(lock, /** 判断首条处理阻塞是否已释放。 */ [&]() { return release_handler; });
		}
		return true;
	});

	ASSERT_EQ(dispatcher.Submit({nullptr, 1, "-1"}), LogicSubmitResult::Accepted);
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 2s, /** 判断首条处理是否已进入。 */ [&]() { return first_handler_started; }));
	}
	for (int index = 0; index < MAX_DEALQUE; ++index) {
		ASSERT_EQ(
			dispatcher.Submit({nullptr, 1, std::to_string(index)}),
			LogicSubmitResult::Accepted);
	}
	EXPECT_EQ(
		dispatcher.Submit({nullptr, 1, "1000001"}),
		LogicSubmitResult::Full);

	{
		std::lock_guard<std::mutex> lock(mutex);
		release_handler = true;
	}
	condition.notify_all();
	dispatcher.Stop();
	ASSERT_EQ(dispatched.size(), static_cast<std::size_t>(MAX_DEALQUE + 1));
	EXPECT_EQ(dispatched.front(), -1);
	EXPECT_EQ(dispatched.back(), MAX_DEALQUE - 1);
}

// T08-LOGIC-04
/** 验证空闲工作线程会被消息或停止唤醒。 */
TEST(LogicDispatcherTests, WaitingWorkerWakesForMessageAndStop) {
	std::promise<void> handled_promise;
	auto handled = handled_promise.get_future();
	LogicDispatcher message_dispatcher(/** 通知消息处理已完成。 */ [&](const LogicMessage&) {
		handled_promise.set_value();
		return true;
	});
	ASSERT_EQ(message_dispatcher.Submit({nullptr, 20, "wake"}), LogicSubmitResult::Accepted);
	ASSERT_EQ(handled.wait_for(2s), std::future_status::ready);
	message_dispatcher.Stop();

	LogicDispatcher idle_dispatcher(/** 提供正常空操作处理器以验证空闲停机。 */ [](const LogicMessage&) { return true; });
	auto stopped = std::async(std::launch::async, /** 在异步任务中停止空闲分发器。 */ [&]() { idle_dispatcher.Stop(); });
	ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
	EXPECT_NO_THROW(stopped.get());
}

// T08-LOGIC-05
/** 验证停止在两秒内排空已接受消息。 */
TEST(LogicDispatcherTests, StopDrainsAcceptedMessagesWithinTwoSeconds) {
	constexpr int message_count = 100;
	std::mutex mutex;
	std::condition_variable condition;
	bool first_handler_started = false;
	bool release_handler = false;
	int handled_count = 0;
	LogicDispatcher dispatcher(/** 累计已处理消息并在首条建立可控屏障。 */ [&](const LogicMessage&) {
		std::unique_lock<std::mutex> lock(mutex);
		++handled_count;
		if (!first_handler_started) {
			first_handler_started = true;
			condition.notify_all();
			condition.wait(lock, /** 判断首条处理阻塞是否已释放。 */ [&]() { return release_handler; });
		}
		return true;
	});

	for (int index = 0; index < message_count; ++index) {
		ASSERT_EQ(dispatcher.Submit({nullptr, 30, std::to_string(index)}), LogicSubmitResult::Accepted);
	}
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 2s, /** 判断首条处理是否已进入。 */ [&]() { return first_handler_started; }));
	}
	auto stopped = std::async(std::launch::async, /** 在异步任务中执行排空停机。 */ [&]() { dispatcher.Stop(); });
	{
		std::lock_guard<std::mutex> lock(mutex);
		release_handler = true;
	}
	condition.notify_all();
	ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
	EXPECT_NO_THROW(stopped.get());
	EXPECT_EQ(handled_count, message_count);
}

// T08-LOGIC-06
/** 验证关闭后提交立即拒绝且重复停止幂等。 */
TEST(LogicDispatcherTests, ClosedDispatcherRejectsImmediatelyAndRepeatedStopIsIdempotent) {
	std::atomic<int> handled_count{0};
	LogicDispatcher dispatcher(/** 累计被调用次数以检测关闭后误处理。 */ [&](const LogicMessage&) {
		handled_count.fetch_add(1);
		return true;
	});

	dispatcher.Stop();
	EXPECT_NO_THROW(dispatcher.Stop());
	const auto started = std::chrono::steady_clock::now();
	EXPECT_EQ(dispatcher.Submit({nullptr, 40, "closed"}), LogicSubmitResult::Closed);
	EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
	EXPECT_EQ(handled_count.load(), 0);
}

// T08-LOGIC-07
/** 验证未知消息编号不阻塞后续合法消息且日志省略正文。 */
TEST(LogicDispatcherTests, UnknownIdDoesNotBlockValidMessageAndLogOmitsBody) {
	ScopedLogCapture logs;
	std::promise<void> valid_handled_promise;
	auto valid_handled = valid_handled_promise.get_future();
	LogicDispatcher dispatcher(/** 只接受指定有效编号并通知处理完成。 */ [&](const LogicMessage& message) {
		if (message.id != 77) {
			return false;
		}
		valid_handled_promise.set_value();
		return true;
	});

	ASSERT_EQ(
		dispatcher.Submit({nullptr, 999, "SYNTHETIC_BODY_MARKER_3A01"}),
		LogicSubmitResult::Accepted);
	ASSERT_EQ(dispatcher.Submit({nullptr, 77, "valid"}), LogicSubmitResult::Accepted);
	ASSERT_EQ(valid_handled.wait_for(2s), std::future_status::ready);
	dispatcher.Stop();

	const auto text = logs.Text();
	EXPECT_NE(text.find("999"), std::string::npos);
	EXPECT_EQ(text.find("SYNTHETIC_BODY_MARKER_3A01"), std::string::npos);
}

// T08-LOGIC-08
/** 验证处理器异常不终止工作线程且日志不暴露请求或异常正文。 */
TEST(LogicDispatcherTests, ThrowingHandlerDoesNotTerminateWorkerOrExposePayload) {
    ScopedLogCapture logs;
    std::promise<void> handled;
    auto ready = handled.get_future();
    LogicDispatcher dispatcher(/** 对指定编号抛含敏感标记的异常，其余消息正常完成。 */ [&](const LogicMessage& message) {
        if (message.id == 90) throw std::runtime_error("PRIVATE_EXCEPTION_PAYLOAD");
        handled.set_value();
        return true;
    });
    ASSERT_EQ(dispatcher.Submit({nullptr, 90, "PRIVATE_REQUEST_PAYLOAD"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(dispatcher.Submit({nullptr, 91, "valid"}), LogicSubmitResult::Accepted);
    ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
    dispatcher.Stop();
    EXPECT_EQ(logs.Text().find("PRIVATE_"), std::string::npos);
}

} // namespace
