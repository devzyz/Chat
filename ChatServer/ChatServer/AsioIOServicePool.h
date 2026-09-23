#pragma once
#include "Singleton.h"
#include "../../common/asio/IOServicePool.h"

/** @brief 为服务提供共享 Asio 执行器与工作线程，停止由公共线程池实现负责。 */
class AsioIOServicePool : public Singleton<AsioIOServicePool>, public common::IOServicePool {
    friend class Singleton<AsioIOServicePool>;
public:
    /** @brief 初始化公共 Asio 池；无参形式使用默认线程数，有参形式使用指定容量。 */
    AsioIOServicePool() = default;
    /** @brief 初始化公共 Asio 池；无参形式使用默认线程数，有参形式使用指定容量。 */
    explicit AsioIOServicePool(std::size_t size) : common::IOServicePool(size) {}
};
