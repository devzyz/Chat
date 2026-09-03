#include "LogicDispatcher.h"
#include "Const.h"

#include <spdlog/spdlog.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

struct LogicDispatcher::Impl {
	explicit Impl(Handler callback)
		: handler(std::move(callback)), worker([this]() { Run(); }) {
	}

	void Run() {
		for (;;) {
			LogicMessage message;
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, [this]() { return stopping || !messages.empty(); });
				if (stopping && messages.empty()) {
					return;
				}
				message = std::move(messages.front());
				messages.pop();
			}
			if (!handler(message)) {
				SPDLOG_WARN("logic message handler not found, msg_id={}", message.id);
			}
		}
	}

	Handler handler;
	std::mutex mutex;
	std::condition_variable condition;
	std::queue<LogicMessage> messages;
	bool stopping{false};
	std::once_flag stop_once;
	std::thread worker;
};

LogicDispatcher::LogicDispatcher(Handler handler)
	: _impl(std::make_unique<Impl>(std::move(handler))) {
}

LogicDispatcher::~LogicDispatcher() {
	Stop();
}

LogicSubmitResult LogicDispatcher::Submit(LogicMessage message) {
	{
		std::lock_guard<std::mutex> lock(_impl->mutex);
		if (_impl->stopping) {
			return LogicSubmitResult::Closed;
		}
		if (_impl->messages.size() >= MAX_DEALQUE) {
			return LogicSubmitResult::Full;
		}
		_impl->messages.push(std::move(message));
	}
	_impl->condition.notify_one();
	return LogicSubmitResult::Accepted;
}

void LogicDispatcher::Stop() {
	std::call_once(_impl->stop_once, [this]() {
		{
			std::lock_guard<std::mutex> lock(_impl->mutex);
			_impl->stopping = true;
		}
		_impl->condition.notify_one();
		if (_impl->worker.joinable()) {
			_impl->worker.join();
		}
	});
}
