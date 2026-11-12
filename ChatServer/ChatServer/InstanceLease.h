#pragma once

#include "DependencyExecutor.h"
#include "Const.h"
#include <spdlog/spdlog.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// 通过共享 Redis 池维护本进程的注册、计数和条件注销。所有者在取消定时器后排空续租，再注销。
class InstanceLease {
public:
    using Values = std::vector<std::string>;
    using Eval = std::function<std::optional<Values>(const std::string&, const Values&, const Values&)>;
    // owner 包含已绑定端点前缀和进程随机标识；从启动到注销保持一致。
    InstanceLease(DependencyExecutor& executor, Eval eval, std::string server, std::string endpoint, std::string nonce)
        : _executor(executor), _eval(std::move(eval)), _server(std::move(server)), _endpoint(std::move(endpoint)),
          _owner(_endpoint + nonce), _keys{LOGIN_COUNT, "chatlease_" + _server} {}
    // 启动主线程等待原子注册，失败不得开放 TCP。
    bool Register() {
        // 相同监听端点可接管已退出实例的遗留租约。
        auto evaluate = [this] {
            return _eval(
                "local old=redis.call('GET',KEYS[2]); "
                "if old and string.sub(old,1,string.len(ARGV[3]))~=ARGV[3] then return {} end; "
                "redis.call('SET',KEYS[2],ARGV[2],'EX',90); "
                "redis.call('HSET',KEYS[1],ARGV[1],0); return {'registered'}",
                _keys,
                {_server, _owner, _endpoint});
        };
        auto result = _executor.SubmitFuture(ExternalTask::Redis, std::move(evaluate)).get();
        return result && *result == Values{"registered"};
    }
    // 定时器只投递，不等待网络；队满或所有者变化可观察。
    void Renew(std::size_t count) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_renewing) {
            _next_count = count;
            return;
        }
        // 提交和设置在途标记共用一把锁，只有 Redis worker 在锁外写实际计数。
        auto status = _executor.SubmitExternal(ExternalTask::Redis, [this, count] {
            RenewLatest(count);
        });
        _renewing = status == LogicSubmitResult::Accepted;
        if (!_renewing)
            SPDLOG_WARN("instance renewal not admitted");
    }
    // 调用前停止续租；等待已有任务完成后再条件注销，依赖失败由租约 TTL 兜底。
    void Unregister() {
        _executor.Drain();
        // 只删除匹配 owner 的租约和计数。
        auto evaluate = [this] {
            return _eval(
                "if redis.call('GET',KEYS[2])==ARGV[2] then "
                "redis.call('HDEL',KEYS[1],ARGV[1]); redis.call('DEL',KEYS[2]); end; return {}",
                _keys,
                {_server, _owner});
        };
        auto result = _executor.SubmitFuture(ExternalTask::Redis, std::move(evaluate)).get();
        if (!result)
            SPDLOG_WARN("instance unregister unavailable");
    }

private:
    // 同一实例一次只写一笔。执行期间到达的多次采样合并为最近的连接数。
    void RenewLatest(std::size_t count) {
        for (;;) {
            try {
                auto result = _eval(
                    "local owner=redis.call('GET',KEYS[2]); if owner and owner~=ARGV[3] then return {} end; "
                    "redis.call('HSET',KEYS[1],ARGV[1],ARGV[2]); "
                    "redis.call('SET',KEYS[2],ARGV[3],'EX',90); return {'renewed'}",
                    _keys,
                    {_server, std::to_string(count), _owner});
                if (!result || *result != Values{"renewed"})
                    SPDLOG_WARN("instance renewal unavailable");
            } catch (...) {
                SPDLOG_WARN("instance renewal failed");
            }
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_next_count) {
                _renewing = false;
                return;
            }
            count = *_next_count;
            _next_count.reset();
        }
    }
    std::mutex _mutex;
    std::optional<std::size_t> _next_count;
    bool _renewing = false;
    DependencyExecutor& _executor;
    Eval _eval;
    std::string _server, _endpoint, _owner;
    Values _keys;
};
