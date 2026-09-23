#pragma once

#include <boost/asio.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace common {
// One thread per context. Call Stop from the owning thread, after sessions close.
/** @brief 为多个 io_context 持有工作守卫及线程，轮转分配执行器，停止时撤销守卫并等待线程。 */
class IOServicePool {
public:
    /** @brief 初始化IOServicePool，为多个 io_context 持有工作守卫及线程，轮转分配执行器，停止时撤销守卫并等待线程。 */
    explicit IOServicePool(std::size_t count = DefaultSize())
        : _contexts(count ? count : 1) {
        for (auto& context : _contexts) {
            _work.emplace_back(std::make_unique<Guard>(context.get_executor()));
        }
        try {
            for (auto& context : _contexts) {
                auto* selected = &context;
                _threads.emplace_back(/** @brief 在所属工作线程运行被选择的 io_context。 */ [selected] { selected->run(); });
            }
        } catch (...) { Stop(); throw; }
    }
    /** @brief 执行服务关闭流程并释放本对象拥有的运行资源。 */
    ~IOServicePool() { Stop(); }
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    IOServicePool(const IOServicePool&) = delete;
    /** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
    IOServicePool& operator=(const IOServicePool&) = delete;
    /** @brief 轮转返回池内 io_context 的借用引用，池必须保持存活。 */
    boost::asio::io_context& GetIOService() {
        return _contexts[_next.fetch_add(1) % _contexts.size()];
    }
    /** @brief 停止接收新工作并关闭当前服务的监听或执行器；后续销毁由所属生命周期流程负责。 */
    void Stop() {
        if (_stopped.exchange(true)) return;
        for (auto& context : _contexts) context.stop();
        for (auto& thread : _threads) if (thread.joinable()) thread.join();
        _work.clear();
    }
    /** @brief 根据硬件并发能力计算至少一个工作线程的默认池大小。 */
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
