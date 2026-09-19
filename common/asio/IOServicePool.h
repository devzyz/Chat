#pragma once

#include <boost/asio.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace common {
// One thread per context. Call Stop from the owning thread, after sessions close.
class IOServicePool {
public:
    explicit IOServicePool(std::size_t count = DefaultSize())
        : _contexts(count ? count : 1) {
        for (auto& context : _contexts) {
            _work.emplace_back(std::make_unique<Guard>(context.get_executor()));
        }
        try {
            for (auto& context : _contexts) {
                auto* selected = &context;
                _threads.emplace_back([selected] { selected->run(); });
            }
        } catch (...) { Stop(); throw; }
    }
    ~IOServicePool() { Stop(); }
    IOServicePool(const IOServicePool&) = delete;
    IOServicePool& operator=(const IOServicePool&) = delete;
    boost::asio::io_context& GetIOService() {
        return _contexts[_next.fetch_add(1) % _contexts.size()];
    }
    void Stop() {
        if (_stopped.exchange(true)) return;
        for (auto& context : _contexts) context.stop();
        for (auto& thread : _threads) if (thread.joinable()) thread.join();
        _work.clear();
    }
    static std::size_t DefaultSize() {
        const auto count = std::thread::hardware_concurrency();
        return count > 1 ? count - 1 : 1;
    }
private:
    using Guard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    std::vector<boost::asio::io_context> _contexts;
    std::vector<std::unique_ptr<Guard>> _work;
    std::vector<std::thread> _threads;
    std::atomic<std::size_t> _next{0};
    std::atomic<bool> _stopped{false};
};
}
