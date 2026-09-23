#pragma once

#include "StatusRouting.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

/** @brief 保存候选实例名称与客户端连接地址，名称用于计数读取和并列排序。 */
struct RoutingServer {
	std::string name;
	std::string host;
	std::string port;
};

namespace status_routing_internal {

/** @brief 隔离实例负载和用户 Token 的同步存储；并发路由要求实现支持并发。 */
class StatusStore {
public:
    /** @brief 允许经存储端口销毁适配器。 */
	virtual ~StatusStore() = default;
    /** @brief 返回原始计数字符串；空值或非法计数按未知处理，抛异常则令整个分配失败。 */
	virtual std::optional<std::string> ReadCount(const std::string& server_name) = 0;
    /** @brief 覆盖 uid 的登录 Token，成功返回 true，false 阻止返回选服结果。 */
	virtual bool PutToken(int uid, const std::string& token) = 0;
    /** @brief 返回 Token 副本，nullopt 在校验层映射为 UidInvalid。 */
	virtual std::optional<std::string> GetToken(int uid) = 0;
};

/** @brief 提供登录 Token，生产实现使用 UUID，测试可注入确定结果。 */
class TokenSource {
public:
    /** @brief 允许经接口销毁 Token 源。 */
	virtual ~TokenSource() = default;
    /** @brief 生成一个 Token；空字符串或异常将导致本次选服失败。 */
	virtual std::string Next() = 0;
};

/** @brief 拥有候选实例副本并共享持有有效的存储及 Token 源，不修改调用方实例列表。 */
std::unique_ptr<StatusRouting> CreateStatusRouting(
	std::vector<RoutingServer> servers,
	std::shared_ptr<StatusStore> store,
	std::shared_ptr<TokenSource> token_source);

} // namespace status_routing_internal
