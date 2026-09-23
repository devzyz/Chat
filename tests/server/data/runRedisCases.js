'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { poll } = require('../../services/dependencyCoordinator');

/** 为原生 Redis 用例构造所属实例的环境，可覆盖故障端口。 */
function environment(coordinator, port = coordinator.config.ports.redis) {
    return { ...process.env, CHAT_REDIS_HOST: coordinator.config.host,
        CHAT_REDIS_PORT: String(port), CHAT_REDIS_PASSWORD: coordinator.password,
        CHAT_REDIS_PREFIX: coordinator.config.prefix };
}

/** 启动原生用例并核对成功标记，限制输出和总时长，支持重启握手。 */
function runNative(coordinator, scenario, options = {}) {
    const binary = process.env.CHAT_REDIS_TEST_BINARY;
    assert.ok(binary && path.isAbsolute(binary), 'native Redis binary must be absolute');
    return new Promise(/** 管理原生进程、超时与输出，退出后统一兑现测试结果。 */ (resolve, reject) => {
        // Inherit the RunContext-owned process group. Never detach nested helpers.
        const child = spawn(binary, [scenario], { env: environment(coordinator, options.port),
            stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
        let output = '';
        let failure;
        let resumed = false;
        let handshake = Promise.resolve();
        const abort = /** 保存主失败并强制终止已启动的测试子进程。 */ (message) => {
            failure = new Error(message);
            child.kill('SIGKILL');
        };
        const timer = setTimeout(/** 在总时限到达时终止原生用例。 */ () => abort('native Redis deadline'), options.onReady ? 45000 : 10000);
        child.on('error', /** 记录进程无法启动的脱敏错误。 */ () => { failure = new Error('native Redis unavailable'); });
        child.stderr.resume();
        child.stdin.on('error', /** 由子进程退出统一报告输入管道关闭，避免未处理错误事件。 */ () => { /* Child exit owns closed stdin failure. */ });
        child.stdout.on('data', /** 收集有界输出，在首次 READY 后执行外部故障注入握手。 */ (chunk) => {
            output += chunk.toString();
            if (output.length > 4096) { abort('native Redis output limit'); return; }
            if (options.onReady && !resumed && output.includes('READY\n')) {
                resumed = true;
                handshake = Promise.resolve().then(options.onReady).then(/** 通知原生用例继续执行重启后的断言。 */ () => {
                    child.stdin.end('RESUME\n');
                }).catch(/** 握手失败时终止子进程并记录重启失败。 */ () => { abort('native Redis restart failed'); });
            }
        });
        child.on('close', /** 等待握手结束，核对退出码及场景标记后决定成功或失败。 */ async (code) => {
            clearTimeout(timer);
            await handshake;
            if (failure) reject(failure);
            else if (code !== 0 || !output.includes(`PASS ${scenario}\n`)) reject(new Error('native Redis assertion failed'));
            else resolve();
        });
        if (!options.onReady) child.stdin.end();
    });
}

/** 创建稳定 loopback 转发端点，跟踪两端连接以支持 Redis 重启断连。 */
async function stableProxy(coordinator) {
    const connections = new Set();
    let nextForward;
    const server = net.createServer(/** 为每个下游连接创建 Redis 上游，并联动两端关闭。 */ (downstream) => {
        const upstream = net.createConnection({ host: coordinator.config.host, port: coordinator.config.ports.redis });
        connections.add(downstream);
        connections.add(upstream);
        const close = /** 销毁同一转发会话的两端套接字。 */ () => { downstream.destroy(); upstream.destroy(); };
        downstream.on('error', close);
        upstream.on('error', close);
        downstream.on('close', /** 移除关闭的下游并销毁其上游。 */ () => { connections.delete(downstream); upstream.destroy(); });
        upstream.on('close', /** 移除关闭的上游并销毁其下游。 */ () => { connections.delete(upstream); downstream.destroy(); });
        downstream.on('data', /** 通知等待者已观察到下游转发数据。 */ () => { nextForward?.(); });
        downstream.pipe(upstream).pipe(downstream);
    });
    await new Promise(/** 监听临时 loopback 端口并传播启动错误。 */ (resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    return { port: server.address().port,
        /** 等待下一次数据转发，超时后移除本次通知钩子。 */
        waitForForward() {
            return new Promise(/** 安装一次性转发通知及有界截止计时器。 */ (resolve, reject) => {
                const timer = setTimeout(/** 清除通知钩子并报告转发等待超时。 */ () => { nextForward = undefined; reject(new Error('proxy forward deadline')); }, 500);
                nextForward = /** 清除截止计时器和通知钩子，完成转发等待。 */ () => { clearTimeout(timer); nextForward = undefined; resolve(); };
            });
        },
        /** 断开当前全部转发连接，保留监听端点供重连。 */
        disconnect() { for (const socket of connections) socket.destroy(); },
        /** 销毁所有会话并等待代理监听器关闭。 */
        async close() {
            for (const socket of connections) socket.destroy();
            await new Promise(/** 把监听器关闭回调转换为可等待的完成信号。 */ (resolve) => server.close(resolve));
        } };
}

/** 重新启动所属 Redis，轮询设置本次运行的认证凭据。 */
async function restoreRedis(coordinator) {
    await coordinator.lifecycle('redis', 'start');
    await poll(/** 尝试配置 Redis 认证，完成或失败后均释放临时客户端。 */ async () => {
        const client = await coordinator.redis(null);
        try { await client.config('SET', 'requirepass', coordinator.password); return true; }
        finally { client.disconnect(); coordinator.clients.delete(client); }
    }, 5000);
}

/** 重启所属 Redis 并断开代理连接，恢复此前证明键及剩余有效期。 */
async function restartRedis(coordinator, proxy) {
    const client = await coordinator.redis();
    let proof;
    let expires;
    try {
        proof = await client.get(`${coordinator.config.prefix}proof`);
        const ttl = await client.pttl(`${coordinator.config.prefix}proof`);
        expires = ttl > 0 ? Date.now() + ttl : null;
    } finally { client.disconnect(); coordinator.clients.delete(client); }
    await coordinator.lifecycle('redis', 'stop');
    proxy.disconnect();
    await restoreRedis(coordinator);
    // Preserve the infrastructure suite's prior proof and its remaining TTL.
    if (proof !== null && expires !== null && expires > Date.now()) {
        const restored = await coordinator.redis();
        try { await restored.set(`${coordinator.config.prefix}proof`, proof, 'PX', Math.max(1, expires - Date.now())); }
        finally { restored.disconnect(); coordinator.clients.delete(restored); }
    }
}

/** 只清理本次 native 前缀下的键，并确认没有残留。 */
async function clearPrefix(coordinator) {
    const client = await coordinator.redis();
    try {
        let cursor = '0';
        do {
            const [next, keys] = await client.scan(cursor, 'MATCH', `${coordinator.config.prefix}native:*`, 'COUNT', 100);
            cursor = next;
            assert.ok(keys.every(/** 确认待删除键属于本次运行的 native 命名空间。 */ (key) => key.startsWith(`${coordinator.config.prefix}native:`)), 'foreign Redis key');
            if (keys.length) await client.del(...keys);
        } while (cursor !== '0');
        const remaining = await client.keys(`${coordinator.config.prefix}native:*`);
        assert.equal(remaining.length, 0);
    } finally { client.disconnect(); coordinator.clients.delete(client); }
}

/** 运行原生 Redis 合同并记录结果，结束后清理所属键且保留主失败。 */
async function runRedisCases(coordinator, record) {
    let primaryFailure;
    try {
        await record('T10-RDS-01', 'native pool finite borrow and close', /** 验证原生连接池有界借用与关闭。 */ () => runNative(coordinator, 'lifecycle'));
        await record('T10-RDS-02', 'native binary read write and atomic TTL expiry', /** 验证二进制读写及原子 TTL 到期。 */ () => runNative(coordinator, 'readwrite'));
        await record('T10-RDS-03', 'native authentication rejection', /** 验证原生客户端拒绝错误认证。 */ () => runNative(coordinator, 'reject'));
        await record('T10-RDS-04', 'native command timeout drops bad connection', /** 验证命令超时淘汰失效连接。 */ () => runNative(coordinator, 'timeout'));
        await record('T10-RDS-05', 'native dead idle connection is revalidated', /** 验证空闲失效连接在再次借用前重建。 */ () => runNative(coordinator, 'reconnect'));
        await record('T10-RDS-09', 'native authentication handshake deadline', /** 使用只接收字节的临时监听器验证 Redis 握手硬超时。 */ async () => {
            const sockets = new Set();
            const listener = net.createServer(/** 登记故障连接并消费数据，但不伪造 Redis 回复。 */ (socket) => {
                sockets.add(socket);
                socket.on('error', /** 发生套接字错误时销毁故障连接。 */ () => socket.destroy());
                socket.on('close', /** 连接关闭后移除其跟踪记录。 */ () => sockets.delete(socket));
                socket.resume(); // Accept bytes but never impersonate a Redis response.
            });
            await new Promise(/** 启动本次握手故障监听器并传播监听失败。 */ (resolve, reject) => { listener.once('error', reject); listener.listen(0, '127.0.0.1', resolve); });
            try { await runNative(coordinator, 'handshake-timeout', { port: listener.address().port }); }
            finally {
                for (const socket of sockets) socket.destroy();
                await new Promise(/** 等待握手故障监听器释放端口。 */ (resolve) => listener.close(resolve));
            }
        });
        await record('T10-RDS-06', 'native connection refusal deadline', /** 释放临时保留端口后验证原生拒连路径，不访问个人服务。 */ async () => {
            // Reserve then close an ephemeral loopback port: no personal endpoint.
            const listener = net.createServer();
            await new Promise(/** 先绑定临时 loopback 端口以获得本场景的端口号。 */ (resolve, reject) => { listener.once('error', reject); listener.listen(0, '127.0.0.1', resolve); });
            const port = listener.address().port;
            await new Promise(/** 释放端口以制造真实连接拒绝。 */ (resolve) => listener.close(resolve));
            await runNative(coordinator, 'refused', { port });
        });
        await record('T10-RDS-07', 'same native pool recovers after real Redis restart', /** 通过稳定代理验证 Redis 重启后恢复，异常时仍恢复依赖并关闭代理。 */ async () => {
            const proxy = await stableProxy(coordinator);
            let needsRestore = false;
            try {
                await runNative(coordinator, 'restart', { port: proxy.port, onReady: /** 在原生 READY 握手点重启所属 Redis，维护失败后恢复标志。 */ async () => {
                    needsRestore = true;
                    await restartRedis(coordinator, proxy);
                    needsRestore = false;
                } });
            } finally {
                await proxy.close();
                if (needsRestore) await restoreRedis(coordinator);
            }
        });
    } catch (error) {
        primaryFailure = error;
    } finally {
        try { await record('T10-RDS-08', 'owned native adapter prefix cleanup', /** 清理本次原生测试键并验证无残留。 */ () => clearPrefix(coordinator)); }
        catch (error) { primaryFailure ??= error; }
    }
    if (primaryFailure) throw primaryFailure;
}

module.exports = { runRedisCases, stableProxy, restartRedis, restoreRedis };
