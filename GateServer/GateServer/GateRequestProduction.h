#pragma once

#include "GateRequest.h"

#include <memory>

namespace gate {

/** @brief 将现有验证码服务、Redis、MySQL 和 Gate 选服模块适配为请求编排器，初始化失败可抛异常。 */
std::unique_ptr<GateRequest> CreateProductionGateRequest();

} // namespace gate
