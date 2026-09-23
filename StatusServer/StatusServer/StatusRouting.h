#pragma once

#include <memory>
#include <string>

/** @brief 保存选服及 Token 发布结果，默认 RPCFailed 且不暴露半完成的地址或 Token。 */
struct AssignmentResult {
	int error = 1002;
	std::string host;
	std::string port;
	std::string token;
};

/** @brief 保存 Token 校验结果；失败时不返回认证身份或 Token。 */
struct LoginResult {
	int error = 1002;
	int uid = 0;
	std::string token;
};

/** @brief 同步选择 ChatServer 并校验 Token；并发安全取决于注入存储和 Token 源。 */
class StatusRouting {
public:
    /** @brief 允许经路由接口销毁实现。 */
	virtual ~StatusRouting() = default;

    /**
     * @brief 优先选择最小有效非负计数，未知计数排后，同值按实例名称排序；写入 Token 后才返回成功。
     * @note 空实例表、空 Token、存储写失败或异常返回 RPCFailed；本操作不预留连接数。
     */
	virtual AssignmentResult Assign(int uid) = 0;
    /** @brief 同步比较保存的 Token；缺失为 UidInvalid，不匹配为 TokenInvalid，异常为 RPCFailed。 */
	virtual LoginResult Validate(int uid, const std::string& token) = 0;
};
