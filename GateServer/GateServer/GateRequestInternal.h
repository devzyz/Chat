#pragma once

#include "GateRequest.h"

#include <memory>
#include <optional>
#include <string>

namespace gate::internal {

/** @brief 保存凭据校验通过后的用户标识，不携带密码。 */
struct UserRecord {
	int uid = 0;
};

/** @brief 保存 Status 选服结果，只有 error 为成功时地址和 Token 才可使用。 */
struct StatusAssignment {
	int error = 1002;
	std::string token;
	std::string host;
	std::string port;
};

/** @brief 同步请求验证码发送的外部服务边界。 */
class VerificationPort {
public:
    /** @brief 允许通过端口销毁验证码适配器。 */
	virtual ~VerificationPort() = default;
    /** @brief 为 email 请求验证码并返回业务错误码；异常由请求编排层映射。 */
	virtual int RequestCode(const std::string& email) = 0;
};

/** @brief 提供验证码读取，不在读取时消费或删除验证码。 */
class CodeStore {
public:
    /** @brief 允许通过端口销毁验证码存储适配器。 */
	virtual ~CodeStore() = default;
    /** @brief 返回验证码副本；nullopt 由编排层按过期处理，异常映射为依赖失败。 */
	virtual std::optional<std::string> ReadCode(const std::string& email) = 0;
};

/** @brief 提供同步账号持久化操作，调用时不拥有请求编排器的锁。 */
class UserStore {
public:
    /** @brief 允许通过端口销毁账号存储适配器。 */
	virtual ~UserStore() = default;
    /** @brief 创建账号并返回 UID；现有编排约定 0 或 -1 为 UserExist，异常为 RPCFailed。 */
	virtual int CreateUser(
		const std::string& username,
		const std::string& email,
		const std::string& password) = 0;
    /** @brief 检查用户名与邮箱匹配；false 由编排层映射为 EmailNotMatch。 */
	virtual bool IdentityMatches(
		const std::string& username,
		const std::string& email) = 0;
    /** @brief 更新指定用户名的密码；false 由编排层映射为 PasswdUpFailed。 */
	virtual bool UpdatePassword(
		const std::string& username,
		const std::string& password) = 0;
    /** @brief 校验邮箱和密码；无匹配返回 nullopt，成功返回用户标识副本。 */
	virtual std::optional<UserRecord> CheckCredentials(
		const std::string& email,
		const std::string& password) = 0;
};

/** @brief 隔离同步 ChatServer 选服和 Token 分配调用。 */
class StatusPort {
public:
    /** @brief 允许通过端口销毁 Status 适配器。 */
	virtual ~StatusPort() = default;
    /** @brief 为已认证 UID 分配地址及 Token，非成功错误由 Gate 映射为 RPCFailed。 */
	virtual StatusAssignment Assign(int uid) = 0;
};

/** @brief 创建共享持有四个有效依赖的编排器；并发调用要求各依赖支持并发，不在此处额外加锁。 */
std::unique_ptr<GateRequest> CreateGateRequest(
	std::shared_ptr<VerificationPort> verification,
	std::shared_ptr<CodeStore> code_store,
	std::shared_ptr<UserStore> user_store,
	std::shared_ptr<StatusPort> status);

} // namespace gate::internal
