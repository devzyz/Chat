#pragma once
#include "Singleton.h"
#include <boost/asio.hpp>
#include <vector>
#include <memory>

class AsioIOServicePool : public Singleton<AsioIOServicePool>
{
	friend class Singleton<AsioIOServicePool>;
public:
	using IOService = boost::asio::io_context;
	using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
	using WorkPtr = std::unique_ptr<Work>;
	
	~AsioIOServicePool();
	boost::asio::io_context& GetIOService();

	AsioIOServicePool(const AsioIOServicePool&) = delete;
	AsioIOServicePool& operator = (const AsioIOServicePool&) = delete;
	void stop();

private:
	AsioIOServicePool(std::size_t size = 2);
	std::size_t _nextIOService;
	std::vector<IOService> _ioServices;
	std::vector<WorkPtr> _works;
	std::vector<std::thread> _threads;
};

