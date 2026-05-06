#include "AsioIOServicePool.h"

AsioIOServicePool::~AsioIOServicePool() {
	stop();
	std::cout << "AsioIOServicePool destruct" << std::endl;
}

// 返回一个io_context
boost::asio::io_context& AsioIOServicePool::GetIOService() {
	auto& service = _ioServices[_nextIOService];
	_nextIOService = (_nextIOService + 1) % _ioServices.size();
	return service;
}

void AsioIOServicePool::stop() {
	// 将假任务消除
	for (auto& work : _works) {
		// 把服务先停止，防止其他人再进行注册
		work->get_executor().context().stop();
		work.reset();
	}

	for (auto& t : _threads) {
		t.join();
	}
}

// 参数是线程的核数
AsioIOServicePool::AsioIOServicePool(std::size_t size) : _ioServices(size), _works(size), _nextIOService(0) {
	for (std::size_t i = 0; i < size; i++) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
	}

	for (std::size_t i = 0; i < _ioServices.size(); i++) {
		_threads.emplace_back([this, i] {
			_ioServices[i].run();
			});
	}
}
