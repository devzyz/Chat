'use strict';

const { poll } = require('./dependencyCoordinator');

async function waitForConnectionCount(coordinator, name, expected, controls) {
    const redis = await coordinator.redis();
    try {
        // CServer publishes every 60 seconds; keep each live controller below its
        // separate 60-second inactivity limit while awaiting the next publication.
        await poll(async () => {
            for (const control of controls) await control.command('snapshot');
            return await redis.hget('logincount', name) === String(expected);
        }, 70000);
    } finally { redis.disconnect(); }
}

module.exports = { waitForConnectionCount };
