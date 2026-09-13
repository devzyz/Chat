'use strict';

function normalizeRedisConfig(config = {}, password = config.password) {
    const host = config.host === 'localhost' ? '127.0.0.1' : config.host;
    const port = config.port;
    const connectTimeoutMs = config.connectTimeoutMs ?? 1000;
    const commandTimeoutMs = config.commandTimeoutMs ?? 1000;
    if (typeof host !== 'string' || host.trim() === '' || !Number.isInteger(port) || port < 1 || port > 65535 ||
        [connectTimeoutMs, commandTimeoutMs].some((value) =>
            !Number.isInteger(value) || value < 1 || value > 60000) ||
        (password !== undefined && typeof password !== 'string')) {
        throw new Error('Invalid Redis endpoint or finite deadline configuration');
    }
    return { host, port, password, connectTimeoutMs, commandTimeoutMs };
}

function createRedisAdapter(configuration, dependencies = {}) {
    const config = normalizeRedisConfig(configuration);
    const Redis = dependencies.Redis || require('ioredis');
    let client;
    let connecting;
    let closed = false;

    function discard(candidate) {
        if (!candidate) return;
        candidate.disconnect();
        if (client === candidate) client = undefined;
    }

    async function connection() {
        if (closed) throw new Error('Redis adapter closed');
        if (client?.status === 'ready') return client;
        if (connecting) return connecting;
        discard(client);
        const candidate = new Redis({
            host: config.host, port: config.port, password: config.password,
            lazyConnect: true, connectTimeout: config.connectTimeoutMs,
            commandTimeout: config.commandTimeoutMs, retryStrategy: null,
            maxRetriesPerRequest: 0, enableOfflineQueue: false,
            autoResendUnfulfilledCommands: false, autoResubscribe: false,
            enableReadyCheck: false
        });
        client = candidate;
        candidate.on('error', () => { /* Awaited operations own failure; never emit raw secrets. */ });
        let timer;
        const deadline = new Promise((resolve, reject) => {
            timer = setTimeout(() => reject(new Error('Redis connection deadline')),
                config.connectTimeoutMs + config.commandTimeoutMs);
        });
        const begin = Promise.resolve().then(() => {
            if (closed) throw new Error('Redis adapter closed');
            return candidate.connect();
        });
        const attempt = Promise.race([begin, deadline]).then(() => {
            if (closed || client !== candidate) throw new Error('Redis adapter closed');
            return candidate;
        }).catch((error) => { discard(candidate); throw error; }).finally(() => clearTimeout(timer));
        connecting = attempt;
        try { return await attempt; }
        finally { if (connecting === attempt) connecting = undefined; }
    }

    async function execute(action, failure) {
        let candidate;
        try {
            candidate = await connection();
            return await action(candidate);
        } catch {
            discard(candidate);
            return failure;
        }
    }

    return {
        GetRedis(key) { return execute((active) => active.get(key), null); },
        QueryRedis(key) { return execute(async (active) => (await active.exists(key)) || null, null); },
        setRedisExpire(key, value, seconds) {
            if (!Number.isSafeInteger(seconds) || seconds <= 0) return Promise.resolve(false);
            return execute(async (active) => (await active.set(key, value, 'EX', seconds)) === 'OK', false);
        },
        async Quit() {
            closed = true;
            discard(client);
            // No QUIT command: disconnect cancels pending I/O without a network wait.
        }
    };
}

module.exports = { createRedisAdapter, normalizeRedisConfig };
