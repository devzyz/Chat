#pragma once
#include "Singleton.h"
#include <boost/asio.hpp>
#include <vector>
#include <memory>

/**
 * @brief 
 * 线程池，用于实现多线程的连接，每个线程跑由一个单独的io_context管理
 * 
 * 解决的问题：某个线程执行io_context.run，他会按照固定的顺序顺序执行，首先是阻塞的等待epoll_wait的返回，epoll_wait会返回就绪事件列表
 * 然后线程会遍历就绪事件列表，将每个事件对应的回调handler，放入io_context内共享的就绪队列，然后再依次从就绪队列中取出回调函数进行执行
 * 直到取不到回调函数，再执行epoll_wait
 * 此时多个线程会有一些不同的状态，正在等待epoll_wait返回，正在打包函数，正在执行回调函数
 * 因此可能一个正在遍历就序列表，另一个正在处理回调函数，因此就会出现某一个session的不同回调会被多个线程并发处理，可能会同时操作session内部的资源，出现竞争
 * 但是可以通过加锁来解决这种资源竞争问题，但是会有很大的开销
 * 
 * 通过AsioIOServicePool，这样能够保证，有多个io_context，每个io_context内部只有一个线程在进行轮询，因此，保证了连接在同一个session上的回调函数
 * 只能被同一个线程处理，因此不会存在资源竞争问题
 * 
 */
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
	std::size_t _nextIOService; // 通过轮询的方式，返回下一个将要被使用的io_context
	std::vector<IOService> _ioServices;
	std::vector<WorkPtr> _works;
	std::vector<std::thread> _threads;
};

