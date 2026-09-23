#pragma once
#include "../../common/redis/RedisPool.h"
#include "Singleton.h"
#include "hiredis/hiredis.h"
#include "const.h"

/** @brief 按调用方给定的端点和容量拥有 Redis 传输池，不构造业务 key。 */
class RedisConfigPool {
public:
    /** @brief 初始化RedisConfigPool，按调用方给定的端点和容量拥有 Redis 传输池，不构造业务 key。 */
    RedisConfigPool(std::size_t pool_size, const char* host, int port, const char* password)
        : _transport(host, port, password, pool_size) {}
    /** @brief 在池的等待策略内借出独占连接；关闭、耗尽或健康检查失败返回空值，使用后须归还。 */
    redisContext* GetConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        return _transport.Borrow(timeout);
    }
    /** @brief 接回借出的连接所有权，关闭或不可用连接由底层池丢弃；归还后不得继续访问。 */
    void ReturnConnection(redisContext* connection) { _transport.Return(connection); }
    /** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
    void Close() { _transport.Close(); }
private:
    chat_redis::RedisPool _transport;
};

/** @brief 执行模块 Redis 命令并检查响应类型；命令失败不自动重放写入。 */
class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	/** @brief 执行服务关闭流程并释放本对象拥有的运行资源。 */
	~RedisMgr();
	/** @brief 读取 Redis 字符串至输出 value，缺失、类型不符或命令失败返回 false。 */
	bool Get(const std::string& key, std::string& value);
	/** @brief 写入 Redis 字符串并验证 OK 响应，失败返回 false。 */
	bool Set(const std::string& key, const std::string& value);
	/** @brief 向 Redis 列表左端压入数据，并核对返回的列表长度。 */
	bool LPush(const std::string& key, const std::string& value);
	/** @brief 从 Redis 列表左端取值至输出参数，空列表或失败返回 false。 */
	bool LPop(const std::string& key, std::string& value);
	/** @brief 向 Redis 列表右端压入数据，并核对返回的列表长度。 */
	bool RPush(const std::string& key, const std::string& value);
	/** @brief 从 Redis 列表右端取值至输出参数，空列表或失败返回 false。 */
	bool RPop(const std::string& key, std::string& value);
	/** @brief 写入 Redis 哈希字段并校验整数响应，返回命令是否成功。 */
	bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
	/** @brief 写入 Redis 哈希字段并校验整数响应，返回命令是否成功。 */
	bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
	/** @brief 读取 Redis 哈希字段至输出 value，缺失、类型不符或命令失败返回 false。 */
	bool HGet(const std::string& key, const std::string& hkey, std::string& value);
	/** @brief 删除 Redis key 并返回操作结果。 */
	bool Del(const std::string& key);
	/** @brief 删除 Redis 哈希字段并返回操作结果。 */
	bool HDel(const std::string& first_key, const std::string& second_key);
	/** @brief 检查 Redis key 是否存在；不可用连接或非法响应不作为存在处理。 */
	bool ExistsKey(const std::string& key);
	/** @brief 停止接受新的借用并释放空闲连接，唤醒等待者；借出资源仍须按原协议归还。 */
	void Close();

	/** @brief 在有限等待内尝试 SET NX EX，成功返回唯一所有者标识，失败返回空字符串；时间参数为秒。 */
	std::string AcquireLock(const std::string& lockName, int lockTimeout, int acquireTimeout);
	/** @brief 仅当锁值仍等于给定所有者标识时原子删除锁，返回是否释放成功。 */
	bool ReleaseLock(const std::string& lockName, const std::string& identifier);
protected:
	/** @brief 初始化RedisMgr，执行模块 Redis 命令并检查响应类型；命令失败不自动重放写入。 */
	RedisMgr();

	std::unique_ptr<RedisConfigPool> _pool;
};
