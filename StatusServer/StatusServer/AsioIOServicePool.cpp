#include "AsioIOServicePool.h"

std::size_t AsioIOServicePool::DefaultPoolSize() {
	auto cpu_count = std::thread::hardware_concurrency();
	if (cpu_count <= 1) {
		return 1;
	}
	return static_cast<std::size_t>(cpu_count - 1);
}

std::size_t AsioIOServicePool::NormalizePoolSize(std::size_t size) {
	return size == 0 ? 1 : size;
}

AsioIOServicePool::AsioIOServicePool() : AsioIOServicePool(DefaultPoolSize()) {
}

AsioIOServicePool::~AsioIOServicePool() {
	stop();
	SPDLOG_DEBUG("AsioIOServicePool destructed");
}

// 返回一个io_context
boost::asio::io_context& AsioIOServicePool::GetIOService() {
	auto& service = _ioServices[_nextIOService];
	_nextIOService = (_nextIOService + 1) % _ioServices.size();
	return service;
}

void AsioIOServicePool::stop() {
	bool expected = false;
	if (!_b_stop.compare_exchange_strong(expected, true)) {
		return;
	}
	// 将假任务消除
	for (auto& work : _works) {
		// 把服务先停止，防止其他人再进行注册
		work->get_executor().context().stop();
		work.reset();
	}

	for (auto& t : _threads) {
		if (t.joinable()) {
			t.join();
		}
	}
}

// 参数是线程的核数
AsioIOServicePool::AsioIOServicePool(std::size_t size) : _ioServices(NormalizePoolSize(size)), _works(NormalizePoolSize(size)), _nextIOService(0) {
	for (std::size_t i = 0; i < _ioServices.size(); i++) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
	}

	for (std::size_t i = 0; i < _ioServices.size(); i++) {
		_threads.emplace_back([this, i] {
			_ioServices[i].run();
			});
	}
}
