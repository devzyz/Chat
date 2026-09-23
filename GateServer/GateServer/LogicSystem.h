#pragma once
#include "const.h"
#include "GateRequest.h"

class HttpConnection;
typedef std::function<void(std::shared_ptr<HttpConnection>)>HttpHandler;
/** @brief 按 HTTP 路径分发请求，将注册、登录及验证码业务委托给 GateRequest。 */
class LogicSystem
{
public:
    /** @brief 注册 HTTP 路由；gate_request 必须在本对象使用期间保持有效。 */
    explicit LogicSystem(gate::GateRequest& gate_request);
    /** @brief 释放路由回调，不销毁外部持有的 GateRequest。 */
    ~LogicSystem() = default;

    /** @brief 执行匹配的 GET 处理器；路径未注册时返回 false。 */
    bool HandleGet(std::string, std::shared_ptr<HttpConnection>);
    /** @brief 执行匹配的 POST 处理器；路径未注册时返回 false。 */
    bool HandlePost(std::string, std::shared_ptr<HttpConnection>);

    /** @brief 注册 GET 路径处理器，已有路径保持原处理器。 */
    void RegisterGetHandler(std::string, HttpHandler);
    /** @brief 注册 POST 路径处理器，已有路径保持原处理器。 */
    void RegisterPostHandler(std::string, HttpHandler);
private:
    gate::GateRequest& _gate_request;
    std::map<std::string, HttpHandler> _post_handlers;
    std::map<std::string, HttpHandler> _get_handlers;
};
