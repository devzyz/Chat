#pragma once

#include "StatusRouting.h"
#include "StatusRoutingInternal.h"

#include <memory>
#include <vector>

/** @brief 将 Redis 存储与 UUID Token 源装配为路由器；依赖初始化失败可抛异常。 */
std::unique_ptr<StatusRouting> CreateProductionStatusRouting(std::vector<RoutingServer> servers);
