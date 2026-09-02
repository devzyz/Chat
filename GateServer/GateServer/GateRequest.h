#pragma once

#include "GateResponse.h"

#include <json/value.h>

namespace gate {

class GateRequest {
public:
	virtual ~GateRequest() = default;

	virtual Result Handle(Endpoint endpoint, const Json::Value& request) = 0;
};

} // namespace gate
