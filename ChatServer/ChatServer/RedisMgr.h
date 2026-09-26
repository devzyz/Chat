#pragma once
#include "../../common/redis/RedisPool.h"
#include "Singleton.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <chrono>
#include <optional>
#include <vector>
#include <hiredis/hiredis.h>

/** @brief 将模块 Redis 配置转为有界传输池；归还原始指针后调用方不得继续使用。 */
class RedisConnectionPool {
public:
    /** @brief 初始化RedisConnectionPool，将模块 Redis 配置转为有界传输池；归还原始指针后调用方不得继续使用。 */
    RedisConnectionPool(const std::string& host, const std::string& port,
        const std::string& password, int pool_size)
        : _transport(host, ParsePort(port), password, pool_size > 0 ? static_cast<std::size_t>(pool_size) : 0) {}
    /** @brief 在池的等待策略内借出独占连接；关闭、耗尽或健康检查失败返回空值，使用后须归还。 */
    redisContext* GetConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        return _transport.Borrow(timeout);
    }
    /** @brief 接回借出的连接所有权，关闭或不可用连接由底层池丢弃；归还后不得继续访问。 */
    void ReturnConnection(redisContext* connection) { _transport.Return(connection); }
    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close() { _transport.Close(); }
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

/**
 *
 * 连接redis的单例类
 */
class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	/** @brief 执行服务关闭流程并释放本对象拥有的运行资源。 */
	~RedisMgr();
	// 结果保存到value内
	/** @brief 读取 Redis 字符串至输出 value，缺失、类型不符或命令失败返回 false。 */
	bool Get(const std::string& key, std::string& value);
	/** @brief 写入 Redis 字符串并验证 OK 响应，失败返回 false。 */
	bool Set(const std::string& key, const std::string& value);
	// 结果保存到value内
	/** @brief 读取 Redis 哈希字段至输出 value，缺失、类型不符或命令失败返回 false。 */
	bool HGet(const std::string& first_key, const std::string& second_key, std::string& value);
	/** @brief 写入 Redis 哈希字段并校验整数响应，返回命令是否成功。 */
	bool HSet(const std::string& first_key, const std::string& second_key, const std::string& value);
	/** @brief 删除 Redis 哈希字段并返回操作结果。 */
	bool HDel(const std::string& first_key, const std::string& second_key);
	/** @brief 删除 Redis key 并返回操作结果。 */
	bool Del(const std::string& key);
    /** @brief 执行给定 Lua 脚本并读取字符串数组，连接或响应失败返回空 optional。 */
    std::optional<std::vector<std::string>> Eval(const std::string& script,
        const std::vector<std::string>& keys, const std::vector<std::string>& arguments);
	/** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
	void Close();

private:
	/** @brief 初始化RedisMgr，执行模块 Redis 命令并检查响应类型；命令失败不自动重放写入。 */
	RedisMgr();
	std::unique_ptr<RedisConnectionPool> _pool;
};
