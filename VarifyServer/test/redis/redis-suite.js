'use strict';

const assert = require('node:assert/strict');
const { createRedisAdapter } = require('../../redis');
const { poll } = require('../../../tests/services/dependencyCoordinator');
const { stableProxy, restartRedis, restoreRedis } = require('../../../tests/server/data/runRedisCases');

function adapterFor(coordinator, overrides = {}) {
    return createRedisAdapter({ host: coordinator.config.host, port: coordinator.config.ports.redis,
        password: coordinator.password, connectTimeoutMs: 250, commandTimeoutMs: 100, ...overrides });
}

async function withAdmin(coordinator, action) {
    const client = await coordinator.redis();
    try { return await action(client); }
    finally { client.disconnect(); coordinator.clients.delete(client); }
}

async function cleanup(coordinator) {
    await withAdmin(coordinator, async (client) => {
        const pattern = `${coordinator.config.prefix}node:*`;
        let cursor = '0';
        do {
            const [next, keys] = await client.scan(cursor, 'MATCH', pattern, 'COUNT', 100);
            cursor = next;
            assert.ok(keys.every((key) => key.startsWith(`${coordinator.config.prefix}node:`)));
            if (keys.length) await client.del(...keys);
        } while (cursor !== '0');
        assert.deepEqual(await client.keys(pattern), []);
    });
}

async function runRedisCases(coordinator, record) {
    const key = `${coordinator.config.prefix}node:value`;
    let primaryFailure;
    try {
        await record('V08-REDIS-01', 'production Node atomic TTL and failed expiry has no orphan', async () => {
            const adapter = adapterFor(coordinator);
            try {
                assert.equal(await adapter.setRedisExpire(key, 'synthetic-value', 1), true);
                assert.equal(await adapter.GetRedis(key), 'synthetic-value');
                assert.equal(await adapter.QueryRedis(key), 1);
                await withAdmin(coordinator, async (client) => {
                    const ttl = await client.pttl(key);
                    assert.ok(ttl >= 0 && ttl <= 1000);
                    await poll(async () => (await client.exists(key)) === 0, 2500);
                });
                // Redis rejects this out-of-range expiry. A split SET/EXPIRE would
                // leave a permanent value, while SET EX fails without any write.
                assert.equal(await adapter.setRedisExpire(key, 'must-not-persist', Number.MAX_SAFE_INTEGER), false);
                await withAdmin(coordinator, async (client) => assert.equal(await client.exists(key), 0));
            } finally { await adapter.Quit(); }
        });
        await record('V08-REDIS-02', 'production Node wrong authentication preserves failure mapping', async () => {
            const adapter = adapterFor(coordinator, { password: `${coordinator.password}-invalid` });
            try {
                const started = performance.now();
                assert.equal(await adapter.GetRedis(key), null);
                assert.equal(await adapter.setRedisExpire(key, 'value', 30), false);
                assert.ok(performance.now() - started < 1500);
            } finally { await adapter.Quit(); }
        });
        await record('V08-REDIS-03', 'real paused Redis bounds commands and replaces bad client', async () => {
            const adapter = adapterFor(coordinator);
            try {
                assert.equal(await adapter.setRedisExpire(key, 'before-pause', 30), true);
                await withAdmin(coordinator, (client) => client.call('CLIENT', 'PAUSE', '400', 'ALL'));
                const started = performance.now();
                assert.equal(await adapter.GetRedis(key), null);
                assert.ok(performance.now() - started < 500);
                // A readiness probe is not an adapter command replay. The failed
                // business call above remains failed and is never retried.
                await withAdmin(coordinator, async (client) => assert.equal(await client.ping(), 'PONG'));
                assert.equal(await adapter.GetRedis(key), 'before-pause');
            } finally { await adapter.Quit(); }
        });
        await record('V08-REDIS-04', 'same production Node adapter recovers after real Redis restart', async () => {
            const proxy = await stableProxy(coordinator);
            const adapter = adapterFor(coordinator, { port: proxy.port });
            let needsRestore = false;
            try {
                assert.equal(await adapter.setRedisExpire(key, 'before-restart', 30), true);
                needsRestore = true;
                await restartRedis(coordinator, proxy);
                needsRestore = false;
                assert.equal(await adapter.setRedisExpire(key, 'after-restart', 30), true);
                assert.equal(await adapter.GetRedis(key), 'after-restart');
            } finally {
                await adapter.Quit();
                await proxy.close();
                if (needsRestore) await restoreRedis(coordinator);
            }
        });
        await record('V08-REDIS-05', 'production close cancels pending real commands and rejects future writes', async () => {
            const proxy = await stableProxy(coordinator);
            const adapter = adapterFor(coordinator, { port: proxy.port, commandTimeoutMs: 1000 });
            try {
                assert.equal(await adapter.setRedisExpire(key, 'before-close', 30), true);
                await withAdmin(coordinator, (client) => client.call('CLIENT', 'PAUSE', '400', 'ALL'));
                const forwarded = proxy.waitForForward();
                const pending = adapter.GetRedis(key);
                await forwarded; // The real command reached the transport before cancellation.
                await adapter.Quit();
                assert.equal(await pending, null);
                assert.equal(await adapter.setRedisExpire(key, 'after-close', 30), false);
                await withAdmin(coordinator, async (client) => assert.equal(await client.get(key), 'before-close'));
            } finally { await adapter.Quit(); await proxy.close(); }
        });
    } catch (error) {
        primaryFailure = error;
    } finally {
        try { await record('V08-REDIS-06', 'production Node suite prefix cleanup', () => cleanup(coordinator)); }
        catch (error) { primaryFailure ??= error; }
    }
    if (primaryFailure) throw primaryFailure;
}

module.exports = { runRedisCases };
