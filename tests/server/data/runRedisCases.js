'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { poll } = require('../../services/dependencyCoordinator');

function environment(coordinator, port = coordinator.config.ports.redis) {
    return { ...process.env, CHAT_REDIS_HOST: coordinator.config.host,
        CHAT_REDIS_PORT: String(port), CHAT_REDIS_PASSWORD: coordinator.password,
        CHAT_REDIS_PREFIX: coordinator.config.prefix };
}

function runNative(coordinator, scenario, options = {}) {
    const binary = process.env.CHAT_REDIS_TEST_BINARY;
    assert.ok(binary && path.isAbsolute(binary), 'native Redis binary must be absolute');
    return new Promise((resolve, reject) => {
        // Inherit the RunContext-owned process group. Never detach nested helpers.
        const child = spawn(binary, [scenario], { env: environment(coordinator, options.port),
            stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
        let output = '';
        let failure;
        let resumed = false;
        let handshake = Promise.resolve();
        const abort = (message) => {
            failure = new Error(message);
            child.kill('SIGKILL');
        };
        const timer = setTimeout(() => abort('native Redis deadline'), options.onReady ? 45000 : 10000);
        child.on('error', () => { failure = new Error('native Redis unavailable'); });
        child.stderr.resume();
        child.stdin.on('error', () => { /* Child exit owns closed stdin failure. */ });
        child.stdout.on('data', (chunk) => {
            output += chunk.toString();
            if (output.length > 4096) { abort('native Redis output limit'); return; }
            if (options.onReady && !resumed && output.includes('READY\n')) {
                resumed = true;
                handshake = Promise.resolve().then(options.onReady).then(() => {
                    child.stdin.end('RESUME\n');
                }).catch(() => { abort('native Redis restart failed'); });
            }
        });
        child.on('close', async (code) => {
            clearTimeout(timer);
            await handshake;
            if (failure) reject(failure);
            else if (code !== 0 || !output.includes(`PASS ${scenario}\n`)) reject(new Error('native Redis assertion failed'));
            else resolve();
        });
        if (!options.onReady) child.stdin.end();
    });
}

async function stableProxy(coordinator) {
    const connections = new Set();
    let nextForward;
    const server = net.createServer((downstream) => {
        const upstream = net.createConnection({ host: coordinator.config.host, port: coordinator.config.ports.redis });
        connections.add(downstream);
        connections.add(upstream);
        const close = () => { downstream.destroy(); upstream.destroy(); };
        downstream.on('error', close);
        upstream.on('error', close);
        downstream.on('close', () => { connections.delete(downstream); upstream.destroy(); });
        upstream.on('close', () => { connections.delete(upstream); downstream.destroy(); });
        downstream.on('data', () => { nextForward?.(); });
        downstream.pipe(upstream).pipe(downstream);
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    return { port: server.address().port,
        waitForForward() {
            return new Promise((resolve, reject) => {
                const timer = setTimeout(() => { nextForward = undefined; reject(new Error('proxy forward deadline')); }, 500);
                nextForward = () => { clearTimeout(timer); nextForward = undefined; resolve(); };
            });
        },
        disconnect() { for (const socket of connections) socket.destroy(); },
        async close() {
            for (const socket of connections) socket.destroy();
            await new Promise((resolve) => server.close(resolve));
        } };
}

async function restoreRedis(coordinator) {
    await coordinator.lifecycle('redis', 'start');
    await poll(async () => {
        const client = await coordinator.redis(null);
        try { await client.config('SET', 'requirepass', coordinator.password); return true; }
        finally { client.disconnect(); coordinator.clients.delete(client); }
    }, 5000);
}

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

async function clearPrefix(coordinator) {
    const client = await coordinator.redis();
    try {
        let cursor = '0';
        do {
            const [next, keys] = await client.scan(cursor, 'MATCH', `${coordinator.config.prefix}native:*`, 'COUNT', 100);
            cursor = next;
            assert.ok(keys.every((key) => key.startsWith(`${coordinator.config.prefix}native:`)), 'foreign Redis key');
            if (keys.length) await client.del(...keys);
        } while (cursor !== '0');
        const remaining = await client.keys(`${coordinator.config.prefix}native:*`);
        assert.equal(remaining.length, 0);
    } finally { client.disconnect(); coordinator.clients.delete(client); }
}

async function runRedisCases(coordinator, record) {
    let primaryFailure;
    try {
        await record('T10-RDS-01', 'native pool finite borrow and close', () => runNative(coordinator, 'lifecycle'));
        await record('T10-RDS-02', 'native binary read write and atomic TTL expiry', () => runNative(coordinator, 'readwrite'));
        await record('T10-RDS-03', 'native authentication rejection', () => runNative(coordinator, 'reject'));
        await record('T10-RDS-04', 'native command timeout drops bad connection', () => runNative(coordinator, 'timeout'));
        await record('T10-RDS-05', 'native dead idle connection is revalidated', () => runNative(coordinator, 'reconnect'));
        await record('T10-RDS-09', 'native authentication handshake deadline', async () => {
            const sockets = new Set();
            const listener = net.createServer((socket) => {
                sockets.add(socket);
                socket.on('error', () => socket.destroy());
                socket.on('close', () => sockets.delete(socket));
                socket.resume(); // Accept bytes but never impersonate a Redis response.
            });
            await new Promise((resolve, reject) => { listener.once('error', reject); listener.listen(0, '127.0.0.1', resolve); });
            try { await runNative(coordinator, 'handshake-timeout', { port: listener.address().port }); }
            finally {
                for (const socket of sockets) socket.destroy();
                await new Promise((resolve) => listener.close(resolve));
            }
        });
        await record('T10-RDS-06', 'native connection refusal deadline', async () => {
            // Reserve then close an ephemeral loopback port: no personal endpoint.
            const listener = net.createServer();
            await new Promise((resolve, reject) => { listener.once('error', reject); listener.listen(0, '127.0.0.1', resolve); });
            const port = listener.address().port;
            await new Promise((resolve) => listener.close(resolve));
            await runNative(coordinator, 'refused', { port });
        });
        await record('T10-RDS-07', 'same native pool recovers after real Redis restart', async () => {
            const proxy = await stableProxy(coordinator);
            let needsRestore = false;
            try {
                await runNative(coordinator, 'restart', { port: proxy.port, onReady: async () => {
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
        try { await record('T10-RDS-08', 'owned native adapter prefix cleanup', () => clearPrefix(coordinator)); }
        catch (error) { primaryFailure ??= error; }
    }
    if (primaryFailure) throw primaryFailure;
}

module.exports = { runRedisCases, stableProxy, restartRedis, restoreRedis };
