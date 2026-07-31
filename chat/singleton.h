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
    Singleton() = default;
    Singleton(const Singleton<T>&) = delete;
    Singleton<T>& operator = (const Singleton<T>&) = delete;
public:
    static std::shared_ptr<T> GetInstance() {
        // 因为为静态变量，只会被初始化一次
        static std::once_flag s_flag;
        // call_once只有当s_flag第一次被定义的时候，才会执行后面的函数
        std::call_once(s_flag, [&]() {
            _instance = std::shared_ptr<T> (new T);
        });

        return _instance;
    }

    ~Singleton() {
        const auto logger = spdlog::default_logger();
        if (logger) {
            SPDLOG_LOGGER_DEBUG(logger, "singleton destructed");
        }
    }

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
