'use strict';

const assert = require('node:assert/strict');
const { createRedisAdapter } = require('../../redis');
const { poll } = require('../../../tests/services/dependencyCoordinator');
const { stableProxy, restartRedis, restoreRedis } = require('../../../tests/server/data/runRedisCases');

/** 以协调器拥有的端点和测试密码创建短期限 Redis 适配器，允许单例覆盖配置。 */ function adapterFor(coordinator, overrides = {}) {
    return createRedisAdapter({ host: coordinator.config.host, port: coordinator.config.ports.redis,
        password: coordinator.password, connectTimeoutMs: 250, commandTimeoutMs: 100, ...overrides });
}

/** 借用管理客户端执行测试动作，结束后断开并移除跟踪。 */ async function withAdmin(coordinator, action) {
    const client = await coordinator.redis();
    try { return await action(client); }
    finally { client.disconnect(); coordinator.clients.delete(client); }
}

/** 只扫描和删除本次运行的 Node 测试前缀，并断言无残留键。 */ async function cleanup(coordinator) {
    await withAdmin(coordinator, /** 分页扫描所属前缀，核对归属后删除并验证清理完成。 */ async (client) => {
        const pattern = `${coordinator.config.prefix}node:*`;
        let cursor = '0';
        do {
            const [next, keys] = await client.scan(cursor, 'MATCH', pattern, 'COUNT', 100);
            cursor = next;
            assert.ok(keys.every(/** 确认待删键属于本次运行的 Node 测试命名空间。 */ (key) => key.startsWith(`${coordinator.config.prefix}node:`)));
            if (keys.length) await client.del(...keys);
        } while (cursor !== '0');
        assert.deepEqual(await client.keys(pattern), []);
    });
}

/** 使用独立依赖协调器执行 Redis 故障合同并清理本轮命名空间。 */ async function runRedisCases(coordinator, record) {
    const key = `${coordinator.config.prefix}node:value`;
    let primaryFailure;
    try {
        await record('V08-REDIS-01', 'production Node atomic TTL and failed expiry has no orphan', /** 验证原子 TTL 与非法过期参数不遗留键。 */ async () => {
            const adapter = adapterFor(coordinator);
            try {
                assert.equal(await adapter.setRedisExpire(key, 'synthetic-value', 1), true);
                assert.equal(await adapter.getRedis(key), 'synthetic-value');
                assert.equal(await adapter.queryRedis(key), 1);
                await withAdmin(coordinator, /** 核对毫秒 TTL 有界并等待键真实过期。 */ async (client) => {
                    const ttl = await client.pttl(key);
                    assert.ok(ttl >= 0 && ttl <= 1000);
                    await poll(/** 查询测试键是否已过期消失。 */ async () => (await client.exists(key)) === 0, 2500);
                });
                // JavaScript's largest safe integer is still a valid Redis EX
                // value: seconds * 1000 fits Redis's signed 64-bit timestamp.
                assert.equal(await adapter.setRedisExpire(key, 'large-ttl', Number.MAX_SAFE_INTEGER), true);
                assert.equal(await adapter.getRedis(key), 'large-ttl');
                await withAdmin(coordinator, /** 核对测试键仍有期限并显式删除。 */ async (client) => {
                    assert.ok(await client.ttl(key) > 0);
                    assert.equal(await client.del(key), 1);
                });
                for (const seconds of [0, Number.MAX_SAFE_INTEGER + 1]) {
                    assert.equal(await adapter.setRedisExpire(key, 'must-not-persist', seconds), false);
                    await withAdmin(coordinator, /** 确认被测写入未留下键。 */ async (client) => assert.equal(await client.exists(key), 0));
                }
                // Zero cannot pass the adapter's validation. Probe Redis directly
                // for server rejection without claiming an adapter failure.
                await withAdmin(coordinator, /** 验证 Redis 拒绝零秒过期且不持久化该值。 */ async (client) => {
                    await assert.rejects(client.set(key, 'must-not-persist', 'EX', 0),
                        /invalid expire time/i);
                    assert.equal(await client.exists(key), 0);
                });
            } finally { await adapter.close(); }
        });
        await record('V08-REDIS-02', 'production Node wrong authentication preserves failure mapping', /** 验证错误密码映射为受限时间内的失败。 */ async () => {
            const adapter = adapterFor(coordinator, { password: `${coordinator.password}-invalid` });
            try {
                const started = performance.now();
                assert.equal(await adapter.getRedis(key), null);
                assert.equal(await adapter.setRedisExpire(key, 'value', 30), false);
                assert.ok(performance.now() - started < 1500);
            } finally { await adapter.close(); }
        });
        await record('V08-REDIS-03', 'real paused Redis bounds commands and replaces bad client', /** 验证暂停 Redis 后命令有期限且坏连接被替换。 */ async () => {
            const adapter = adapterFor(coordinator);
            try {
                assert.equal(await adapter.setRedisExpire(key, 'before-pause', 30), true);
                await withAdmin(coordinator, /** 暂停 Redis 命令处理以制造可重复的超时。 */ (client) => client.call('CLIENT', 'PAUSE', '400', 'ALL'));
                const started = performance.now();
                assert.equal(await adapter.getRedis(key), null);
                assert.ok(performance.now() - started < 500);
                // A readiness probe is not an adapter command replay. The failed
                // business call above remains failed and is never retried.
                await withAdmin(coordinator, /** 验证 Redis 恢复响应 PING。 */ async (client) => assert.equal(await client.ping(), 'PONG'));
                assert.equal(await adapter.getRedis(key), 'before-pause');
            } finally { await adapter.close(); }
        });
        await record('V08-REDIS-04', 'same production Node adapter recovers after real Redis restart', /** 验证稳定代理下 Redis 重启后的恢复。 */ async () => {
            const proxy = await stableProxy(coordinator);
            const adapter = adapterFor(coordinator, { port: proxy.port });
            let needsRestore = false;
            try {
                assert.equal(await adapter.setRedisExpire(key, 'before-restart', 30), true);
                needsRestore = true;
                await restartRedis(coordinator, proxy);
                needsRestore = false;
                assert.equal(await adapter.setRedisExpire(key, 'after-restart', 30), true);
                assert.equal(await adapter.getRedis(key), 'after-restart');
            } finally {
                await adapter.close();
                await proxy.close();
                if (needsRestore) await restoreRedis(coordinator);
            }
        });
        await record('V08-REDIS-05', 'production close cancels pending real commands and rejects future writes', /** 验证关闭取消在途命令，并拒绝后续写入。 */ async () => {
            const proxy = await stableProxy(coordinator);
            const adapter = adapterFor(coordinator, { port: proxy.port, commandTimeoutMs: 1000 });
            try {
                assert.equal(await adapter.setRedisExpire(key, 'before-close', 30), true);
                await withAdmin(coordinator, /** 暂停 Redis 命令处理以覆盖关闭期间的未完成请求。 */ (client) => client.call('CLIENT', 'PAUSE', '400', 'ALL'));
                const forwarded = proxy.waitForForward();
                const pending = adapter.getRedis(key);
                await forwarded; // The real command reached the transport before cancellation.
                await adapter.close();
                assert.equal(await pending, null);
                assert.equal(await adapter.setRedisExpire(key, 'after-close', 30), false);
                await withAdmin(coordinator, /** 验证关闭过程未破坏此前保存的值。 */ async (client) => assert.equal(await client.get(key), 'before-close'));
            } finally { await adapter.close(); await proxy.close(); }
        });
    } catch (error) {
        primaryFailure = error;
    } finally {
        try { await record('V08-REDIS-06', 'production Node suite prefix cleanup', /** 清理本次运行拥有的 Redis 测试键。 */ () => cleanup(coordinator)); }
        catch (error) { primaryFailure ??= error; }
    }
    if (primaryFailure) throw primaryFailure;
}

module.exports = { runRedisCases };
