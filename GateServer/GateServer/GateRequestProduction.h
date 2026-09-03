#pragma once

#include "GateRequest.h"

#include <memory>

namespace gate {

std::unique_ptr<GateRequest> CreateProductionGateRequest();

} // namespace gate
