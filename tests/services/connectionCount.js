'use strict';

const { poll } = require('./dependencyCoordinator');

/** 轮询 Redis 中的实例连接数，同时维持控制客户端活跃，结束后释放 Redis 连接。 */
async function waitForConnectionCount(coordinator, name, expected, controls) {
    const redis = await coordinator.redis();
    try {
        // CServer publishes every 60 seconds; keep each live controller below its
        // separate 60-second inactivity limit while awaiting the next publication.
        await poll(/** 向活跃控制器发快照命令并检查目标实例的连接计数。 */ async () => {
            for (const control of controls) await control.command('snapshot');
            return await redis.hget('logincount', name) === String(expected);
        }, 70000);
    } finally { redis.disconnect(); }
}

module.exports = { waitForConnectionCount };
