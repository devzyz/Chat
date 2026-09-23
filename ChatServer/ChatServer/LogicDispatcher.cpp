#include "LogicDispatcher.h"
#include "Const.h"

#include <spdlog/spdlog.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

/** @brief 拥有外层服务的运行状态和异步操作所需资源，生命周期由外层实现约束。 */
struct LogicDispatcher::Impl {
	/** @brief 初始化Impl，拥有外层服务的运行状态和异步操作所需资源，生命周期由外层实现约束。 */
	explicit Impl(Handler callback)
		: handler(std::move(callback)), worker(/** @brief 在独立业务线程消费消息队列。 */ [this]() { Run(); }) {
	}

	/** @brief 执行队列中的工作并把结果交回所属执行器，遵守停止状态与错误映射。 */
	void Run() {
		for (;;) {
			LogicMessage message;
			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, /** @brief 仅在停服或队列非空时唤醒消费者。 */ [this]() { return stopping || !messages.empty(); });
				if (stopping && messages.empty()) {
					return;
				}
				message = std::move(messages.front());
				messages.pop();
			}
            try {
                if (!handler(message)) {
                    SPDLOG_WARN("logic message handler not found, msg_id={}", message.id);
                }
            } catch (...) {
                // Malformed field conversions and dependency failures must not
                // escape the worker. Exception text may contain private payloads.
                SPDLOG_ERROR("logic message handler failed, msg_id={}", message.id);
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
	std::call_once(_impl->stop_once, /** @brief 一次性标记停止、唤醒消费者并等待工作线程结束。 */ [this]() {
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
