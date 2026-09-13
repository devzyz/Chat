'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const { once } = require('node:events');
const test = require('node:test');
const { createRedisAdapter } = require('../../redis');

async function blackhole(t) {
    const sockets = new Set();
    const server = net.createServer((socket) => {
        sockets.add(socket);
        socket.on('error', () => socket.destroy());
        socket.on('close', () => sockets.delete(socket));
        socket.resume();
    });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    t.after(async () => {
        for (const socket of sockets) socket.destroy();
        await new Promise((resolve) => server.close(resolve));
    });
    return server;
}

// V08-LOOP-01
test('real non-responding AUTH is bounded and returns null without secret diagnostics', { timeout: 3000 }, async (t) => {
    const server = await blackhole(t);
    const adapter = createRedisAdapter({ host: '127.0.0.1', port: server.address().port,
        password: 'synthetic-only', connectTimeoutMs: 100, commandTimeoutMs: 100 });
    t.after(() => adapter.Quit());
    const started = performance.now();
    assert.equal(await adapter.GetRedis('chat:loop:missing'), null);
    assert.ok(performance.now() - started < 1000);
});

// V08-LOOP-02
test('close cancels an in-flight real handshake and future work', { timeout: 3000 }, async (t) => {
    const server = await blackhole(t);
    const connected = once(server, 'connection');
    const adapter = createRedisAdapter({ host: '127.0.0.1', port: server.address().port,
        password: 'synthetic-only', connectTimeoutMs: 1000, commandTimeoutMs: 1000 });
    t.after(() => adapter.Quit());
    const operation = adapter.GetRedis('chat:loop:missing');
    await connected;
    const started = performance.now();
    await adapter.Quit();
    assert.equal(await operation, null);
    assert.ok(performance.now() - started < 500);
    assert.equal(await adapter.setRedisExpire('chat:loop:missing', 'value', 1), false);
});

// V08-LOOP-03
test('closed ephemeral loopback endpoint fails without retries', { timeout: 3000 }, async () => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const port = server.address().port;
    await new Promise((resolve) => server.close(resolve));
    const adapter = createRedisAdapter({ host: '127.0.0.1', port, connectTimeoutMs: 100, commandTimeoutMs: 100 });
    try {
        const started = performance.now();
        assert.equal(await adapter.GetRedis('chat:loop:missing'), null);
        assert.ok(performance.now() - started < 1000);
    } finally { await adapter.Quit(); }
});
