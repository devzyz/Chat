#include "AsioIOServicePool.h"

AsioIOServicePool::AsioIOServicePool(std::size_t poolSize) : 
	_ioServices(poolSize), _works(poolSize) {
	for (int i = 0; i < poolSize; i++) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
	}
}

AsioIOServicePool::~AsioIOServicePool() {

}

boost::asio::io_context& AsioIOServicePool::GetIOService() {

}

void AsioIOServicePool::stop() {

}
