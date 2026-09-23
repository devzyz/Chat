'use strict';

/** 校验 Redis 地址和有限毫秒期限并返回规范配置，非法输入抛异常。 */
function normalizeRedisConfig(config = {}, password = config.password) {
    const host = config.host === 'localhost' ? '127.0.0.1' : config.host;
    const port = config.port;
    const connectTimeoutMs = config.connectTimeoutMs ?? 1000;
    const commandTimeoutMs = config.commandTimeoutMs ?? 1000;
    if (typeof host !== 'string' || host.trim() === '' || !Number.isInteger(port) || port < 1 || port > 65535 ||
        [connectTimeoutMs, commandTimeoutMs].some(
            /** 拒绝非整数、非正数或过大的期限。 */
            (value) =>
            !Number.isInteger(value) || value < 1 || value > 60000) ||
        (password !== undefined && typeof password !== 'string')) {
        throw new Error('Invalid Redis endpoint or finite deadline configuration');
    }
    return { host, port, password, connectTimeoutMs, commandTimeoutMs };
}

/** 创建惰性 Redis 适配器，并发首次调用共享连接尝试；命令失败返回约定哨兵。 */
function createRedisAdapter(configuration, dependencies = {}) {
    const config = normalizeRedisConfig(configuration);
    const Redis = dependencies.Redis || require('ioredis');
    let client;
    let connecting;
    let closed = false;

    /** 断开候选连接，仅清空仍指向该连接的活动引用。 */
    function discard(candidate) {
        if (!candidate) return;
        candidate.disconnect();
        if (client === candidate) client = undefined;
    }

    /** 返回可用连接或共享建连 Promise；关闭、超时和连接失败均拒绝。 */
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
        candidate.on('error',
            /** 连接错误由等待中的操作负责处理，此监听器避免未处理事件且不输出敏感文本。 */
            () => { /* Awaited operations own failure; never emit raw secrets. */ });
        let timer;
        const deadline = new Promise(
            /** 建立有限建连期限并保存清理句柄。 */
            (resolve, reject) => {
            timer = setTimeout(
                /** 期限到达时拒绝连接等待。 */
                () => reject(new Error('Redis connection deadline')),
                config.connectTimeoutMs + config.commandTimeoutMs);
        });
        const begin = Promise.resolve().then(
            /** 微任务开始时先检查关闭状态，再发起真实连接。 */
            () => {
            if (closed) throw new Error('Redis adapter closed');
            return candidate.connect();
        });
        const attempt = Promise.race([begin, deadline]).then(
            /** 只接受仍属于此适配器且尚未关闭的连接结果。 */
            () => {
            if (closed || client !== candidate) throw new Error('Redis adapter closed');
            return candidate;
        }).catch(
            /** 连接失败时丢弃候选并继续传播异常。 */
            (error) => { discard(candidate); throw error; }).finally(
            /** 无论建连成功或失败都清理期限计时器。 */
            () => clearTimeout(timer));
        connecting = attempt;
        try { return await attempt; }
        finally { if (connecting === attempt) connecting = undefined; }
    }

    /** 执行命令并等待结果；失败断开候选连接并返回指定失败哨兵。 */
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
        /** 异步读取键；不存在或命令失败都返回 null，由上层决定是否重建验证码。 */
        getRedis(key) { return execute(
            /** 在已连接客户端执行 GET。 */
            (active) => active.get(key), null); },
        /** 异步查询键存在数；存在返回 1，不存在或失败返回 null，不返回布尔值。 */
        queryRedis(key) { return execute(
            /** 执行 EXISTS 并将零结果映射为 null。 */
            async (active) => (await active.exists(key)) || null, null); },
        /** 原子执行 SET EX；seconds 必须为正安全整数，失败返回 false。 */
        setRedisExpire(key, value, seconds) {
            if (!Number.isSafeInteger(seconds) || seconds <= 0) return Promise.resolve(false);
            return execute(
                /** 将原子写入的 OK 回复转换为成功布尔值。 */
                async (active) => (await active.set(key, value, 'EX', seconds)) === 'OK', false);
        },
        /** 幂等标记关闭并断开连接，不等待网络 QUIT；以后操作返回失败哨兵。 */
        async close() {
            closed = true;
            discard(client);
            // No QUIT command: disconnect cancels pending I/O without a network wait.
        }
    };
}

module.exports = { createRedisAdapter, normalizeRedisConfig };
