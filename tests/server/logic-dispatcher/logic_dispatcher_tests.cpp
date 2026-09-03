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

class ScopedLogCapture {
public:
	ScopedLogCapture()
		: previous_(spdlog::default_logger()),
		  sink_(std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_)),
		  logger_(std::make_shared<spdlog::logger>("logic-dispatcher-test", sink_)) {
		logger_->set_pattern("%v");
		logger_->set_level(spdlog::level::trace);
		spdlog::set_default_logger(logger_);
	}

	~ScopedLogCapture() {
		spdlog::set_default_logger(previous_);
	}

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
TEST(LogicDispatcherTests, AcceptedMessagesDispatchInFifoOrder) {
	std::mutex mutex;
	std::condition_variable dispatched;
	std::vector<std::int16_t> ids;
	LogicDispatcher dispatcher([&](const LogicMessage& message) {
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
		ASSERT_TRUE(dispatched.wait_for(lock, 2s, [&]() { return ids.size() == 3; }));
	}
	dispatcher.Stop();
	EXPECT_EQ(ids, (std::vector<std::int16_t>{11, 12, 13}));
}

// T08-LOGIC-02
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
	LogicDispatcher dispatcher([&](const LogicMessage& message) {
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
		producers.emplace_back([&, producer]() {
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, [&]() { return start; });
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
		ASSERT_TRUE(condition.wait_for(lock, 2s, [&]() { return dispatched_count == message_count; }));
	}
	dispatcher.Stop();
	EXPECT_EQ(std::count(results.begin(), results.end(), LogicSubmitResult::Accepted), message_count);
	EXPECT_TRUE(std::all_of(dispatch_counts.begin(), dispatch_counts.end(), [](int count) { return count == 1; }));
}

// T08-LOGIC-03
TEST(LogicDispatcherTests, ExactPendingCapacityRejectsOnlyTheNextMessage) {
	std::mutex mutex;
	std::condition_variable condition;
	bool first_handler_started = false;
	bool release_handler = false;
	std::vector<int> dispatched;
	LogicDispatcher dispatcher([&](const LogicMessage& message) {
		std::unique_lock<std::mutex> lock(mutex);
		dispatched.push_back(std::stoi(message.body));
		if (!first_handler_started) {
			first_handler_started = true;
			condition.notify_all();
			condition.wait(lock, [&]() { return release_handler; });
		}
		return true;
	});

	ASSERT_EQ(dispatcher.Submit({nullptr, 1, "-1"}), LogicSubmitResult::Accepted);
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 2s, [&]() { return first_handler_started; }));
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
TEST(LogicDispatcherTests, WaitingWorkerWakesForMessageAndStop) {
	std::promise<void> handled_promise;
	auto handled = handled_promise.get_future();
	LogicDispatcher message_dispatcher([&](const LogicMessage&) {
		handled_promise.set_value();
		return true;
	});
	ASSERT_EQ(message_dispatcher.Submit({nullptr, 20, "wake"}), LogicSubmitResult::Accepted);
	ASSERT_EQ(handled.wait_for(2s), std::future_status::ready);
	message_dispatcher.Stop();

	LogicDispatcher idle_dispatcher([](const LogicMessage&) { return true; });
	auto stopped = std::async(std::launch::async, [&]() { idle_dispatcher.Stop(); });
	ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
	EXPECT_NO_THROW(stopped.get());
}

// T08-LOGIC-05
TEST(LogicDispatcherTests, StopDrainsAcceptedMessagesWithinTwoSeconds) {
	constexpr int message_count = 100;
	std::mutex mutex;
	std::condition_variable condition;
	bool first_handler_started = false;
	bool release_handler = false;
	int handled_count = 0;
	LogicDispatcher dispatcher([&](const LogicMessage&) {
		std::unique_lock<std::mutex> lock(mutex);
		++handled_count;
		if (!first_handler_started) {
			first_handler_started = true;
			condition.notify_all();
			condition.wait(lock, [&]() { return release_handler; });
		}
		return true;
	});

	for (int index = 0; index < message_count; ++index) {
		ASSERT_EQ(dispatcher.Submit({nullptr, 30, std::to_string(index)}), LogicSubmitResult::Accepted);
	}
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(condition.wait_for(lock, 2s, [&]() { return first_handler_started; }));
	}
	auto stopped = std::async(std::launch::async, [&]() { dispatcher.Stop(); });
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
TEST(LogicDispatcherTests, ClosedDispatcherRejectsImmediatelyAndRepeatedStopIsIdempotent) {
	std::atomic<int> handled_count{0};
	LogicDispatcher dispatcher([&](const LogicMessage&) {
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
TEST(LogicDispatcherTests, UnknownIdDoesNotBlockValidMessageAndLogOmitsBody) {
	ScopedLogCapture logs;
	std::promise<void> valid_handled_promise;
	auto valid_handled = valid_handled_promise.get_future();
	LogicDispatcher dispatcher([&](const LogicMessage& message) {
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

} // namespace
