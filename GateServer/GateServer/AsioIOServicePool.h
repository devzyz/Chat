#pragma once
#include "Singleton.h"
#include "../../common/asio/IOServicePool.h"

class AsioIOServicePool : public Singleton<AsioIOServicePool>, public common::IOServicePool {
    friend class Singleton<AsioIOServicePool>;
public:
    AsioIOServicePool() = default;
    explicit AsioIOServicePool(std::size_t size) : common::IOServicePool(size) {}
    void stop() { Stop(); }
};
