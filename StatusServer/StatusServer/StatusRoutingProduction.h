#pragma once

#include "StatusRouting.h"
#include "StatusRoutingInternal.h"

#include <memory>
#include <vector>

std::unique_ptr<StatusRouting> CreateProductionStatusRouting(std::vector<RoutingServer> servers);
