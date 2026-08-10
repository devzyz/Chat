#include "AsioIOServicePool.h"
#include "LogMgr.h"

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

AsioIOServicePool::AsioIOServicePool(std::size_t poolSize) : 
	_ioServices(NormalizePoolSize(poolSize)), _works(NormalizePoolSize(poolSize)), _nextIOService(0), _b_stop(false) {
	for (std::size_t i = 0; i < _ioServices.size(); i++) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
	}

	for (std::size_t i = 0; i < _ioServices.size(); i++) {
		_threads.emplace_back([this, i]() {
			_ioServices[i].run();
			});
	}
}

AsioIOServicePool::~AsioIOServicePool() {
	stop();
	SPDLOG_DEBUG("AsioIOServicePool destructed");
}

boost::asio::io_context& AsioIOServicePool::GetIOService() {
	auto& context = _ioServices[_nextIOService];
	_nextIOService = (_nextIOService + 1) % _ioServices.size();
	return context;
}

/**
 * @brief 
 * 将每个线程的哨兵事件停止，这样每个线程在处理完所有异步操作后，就会返回
 */
void AsioIOServicePool::stop() {
	if (_b_stop) {
		return;
	}
	_b_stop = true;
	for (auto& work : _works) {
		work->get_executor().context().stop();
		work.reset();
	}

	for (auto& thread : _threads) {
		thread.join();
	}
}
