#pragma once
#include "../../common/redis/RedisPool.h"
#include "Singleton.h"
#include "DependencyExecutor.h"
#include "../../common/auth/RedisToken.h"
#include <memory>
#include <chrono>
#include <string>
#include <optional>
#include <vector>
#include <hiredis/hiredis.h>

/** @brief 将模块 Redis 配置转为有界传输池；归还原始指针后调用方不得继续使用。 */
class RedisConnectionPool {
public:
    /** @brief 初始化RedisConnectionPool，将模块 Redis 配置转为有界传输池；归还原始指针后调用方不得继续使用。 */
    RedisConnectionPool(const std::string& host, const std::string& port, const std::string& password, int pool_size)
        : _transport(host, ParsePort(port), password, pool_size > 0 ? static_cast<std::size_t>(pool_size) : 0) {}
    /** @brief 在池的等待策略内借出独占连接；关闭、耗尽或健康检查失败返回空值，使用后须归还。 */
    redisContext* GetConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        return _transport.Borrow(timeout);
    }
    /** @brief 接回借出的连接所有权，关闭或不可用连接由底层池丢弃；归还后不得继续访问。 */
    void ReturnConnection(redisContext* connection) {
        _transport.Return(connection);
    }
    /** @brief 使用本池读取 UID 当前凭据，不刷新 TTL。 */
    chat_auth::TokenResult ValidateToken(int uid, const std::string& token) {
        return chat_auth::ValidateToken(_transport, uid, token);
    }
    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close() {
        _transport.Close();
    }

private:
    /** @brief 解析完整十进制端口，非数字、越界或含尾部内容时抛异常。 */
    static int ParsePort(const std::string& port) {
        std::size_t parsed = 0;
        const int value = std::stoi(port, &parsed);
        if (parsed != port.size() || value < 1 || value > 65535) {
            throw std::invalid_argument("invalid Redis port");
        }
        return value;
    }

    chat_redis::RedisPool _transport;
};

// ChatServer 的 Redis 命令入口，在线状态和租约使用 Lua，登录凭据使用 token 校验。
class RedisMgr : public Singleton<RedisMgr> {
    friend class Singleton<RedisMgr>;

public:
    /** @brief 执行服务关闭流程并释放本对象拥有的运行资源。 */
    ~RedisMgr();
    /** @brief 执行给定 Lua 脚本并读取字符串数组，连接或响应失败返回空 optional。 */
    std::optional<std::vector<std::string>> Eval(
        const std::string& script,
        const std::vector<std::string>& keys,
        const std::vector<std::string>& arguments);
    /** @brief 校验 TCP 登录凭据，区分用户缺失、凭据错误及依赖失败。 */
    chat_auth::TokenResult ValidateToken(int uid, const std::string& token) {
        return CheckedConnectionPool().ValidateToken(uid, token);
    }
    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close();

private:
    /** @brief 初始化RedisMgr，执行模块 Redis 命令并检查响应类型；命令失败不自动重放写入。 */
    RedisMgr();
    // 所有生产命令均在 Redis 工作线程借连接，防止新调用点绕过准入。
    RedisConnectionPool& CheckedConnectionPool() {
        if (!DependencyExecutor::RunningOn(ExternalTask::Redis))
            throw std::logic_error("Redis command outside Redis pool");
        return *_pool;
    }
    std::unique_ptr<RedisConnectionPool> _pool;
};
