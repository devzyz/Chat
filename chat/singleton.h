#ifndef SINGLETON_H
#define SINGLETON_H

#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>

/**
 * @brief The Singleton class
 * 单例模式
 */
template <typename T>
class Singleton {
private:
    static std::shared_ptr<T> _instance;
protected:
    /** @brief 仅允许派生类构造单例基类。 */
    Singleton() = default;
    /** @brief 禁止复制该对象，避免共享可变状态或重复管理资源。 */
    Singleton(const Singleton<T>&) = delete;
    /** @brief 禁止复制赋值，避免产生多个单例状态持有者。 */
    Singleton<T>& operator = (const Singleton<T>&) = delete;
public:
    /** @brief 通过 call_once 初始化共享实例并返回强引用；ReleaseInstance 后不会再次自动构造。 */
    static std::shared_ptr<T> GetInstance() {
        // 因为为静态变量，只会被初始化一次
        static std::once_flag s_flag;
        // call_once只有当s_flag第一次被定义的时候，才会执行后面的函数
        std::call_once(s_flag, /** @brief 首次调用时创建共享实例，后续调用复用该初始化结果。 */ [&]() {
            _instance = std::shared_ptr<T> (new T);
        });

        return _instance;
    }

    /** @brief 释放静态实例引用，不重置 call_once；调用方须确保没有并发访问。 */
    static void ReleaseInstance() {
        _instance.reset();
    }

    /** @brief 日志器仍可用时记录实例析构。 */
    ~Singleton() {
        const auto logger = spdlog::default_logger();
        if (logger) {
            SPDLOG_LOGGER_DEBUG(logger, "singleton destructed");
        }
    }

    /** @brief 在日志可用时记录当前单例地址，供生命周期诊断。 */
    void PrintAddress() {
        const auto logger = spdlog::default_logger();
        if (logger) {
            SPDLOG_LOGGER_DEBUG(
                logger,
                "singleton address={}",
                static_cast<const void*>(_instance.get()));
        }
    }
};

template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;

#endif // SINGLETON_H
