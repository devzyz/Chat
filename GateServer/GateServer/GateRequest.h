#pragma once

#include "GateResponse.h"

#include <json/value.h>

namespace gate {

/** @brief 同步编排验证码、注册、重置密码和登录；返回业务结果，不负责 HTTP 解析或序列化。 */
class GateRequest {
public:
    /** @brief 允许通过请求接口销毁具体编排器。 */
	virtual ~GateRequest() = default;

    /** @brief 执行指定端点；依赖异常映射为 RPCFailed，登录成功才携带 UID、Token 和地址。 */
	virtual Result Handle(Endpoint endpoint, const Json::Value& request) = 0;
};

} // namespace gate
