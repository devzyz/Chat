#include "AsioIOServicePool.h"

AsioIOServicePool::AsioIOServicePool(std::size_t poolSize) : 
	_ioServices(poolSize), _works(poolSize), _nextIOService(0) {
	for (int i = 0; i < poolSize; i++) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
	}

	for (int i = 0; i < _ioServices.size(); i++) {
		_threads.emplace_back([this, i]() {
			_ioServices[i].run();
			});
	}
}

AsioIOServicePool::~AsioIOServicePool() {
	std::cout << "AsioIOServicePool destruct" << std::endl;
}

boost::asio::io_context& AsioIOServicePool::GetIOService() {
	auto& context = _ioServices[_nextIOService];
	_nextIOService = (_nextIOService + 1) % _ioServices.size();
	return context;
}

void AsioIOServicePool::stop() {
	for (auto& work : _works) {
		work->get_executor().context().stop();
		work.reset();
	}

	for (auto& thread : _threads) {
		thread.join();
	}
}
