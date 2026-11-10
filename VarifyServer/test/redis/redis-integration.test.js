'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const { once } = require('node:events');
const test = require('node:test');
const { createRedisAdapter } = require('../../redis');

// 建立接收但不响应的回环 TCP 对端，并注册套接字和监听器清理。
async function blackhole(t) {
    const sockets = new Set();
    // 跟踪连接并消费输入，刻意不发送 Redis 响应以触发期限。
    const server = net.createServer((socket) => {
        sockets.add(socket);
        // 连接出错时销毁套接字。
        socket.on('error', () => socket.destroy());
        // 连接关闭后从跟踪集合移除。
        socket.on('close', () => sockets.delete(socket));
        socket.resume();
    });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    // 销毁剩余连接并等待监听器关闭。
    t.after(async () => {
        for (const socket of sockets) socket.destroy();
        // 将监听器关闭完成转换为 Promise。
        await new Promise((resolve) => server.close(resolve));
    });
    return server;
}

// V08-LOOP-01
// 验证静默 Redis 对端受建连期限约束。
test('real non-responding AUTH is bounded and returns null without secret diagnostics', { timeout: 3000 }, async (t) => {
    const server = await blackhole(t);
    const adapter = createRedisAdapter({ host: '127.0.0.1', port: server.address().port,
        password: 'synthetic-only', connectTimeoutMs: 100, commandTimeoutMs: 100 });
    // 测试结束关闭 Redis 适配器。
    t.after(() => adapter.close());
    const started = performance.now();
    assert.equal(await adapter.getRedis('chat:loop:missing'), null);
    assert.ok(performance.now() - started < 1000);
});

// V08-LOOP-02
// 验证主动关闭取消正在建立的连接。
test('close cancels an in-flight real handshake and future work', { timeout: 3000 }, async (t) => {
    const server = await blackhole(t);
    const connected = once(server, 'connection');
    const adapter = createRedisAdapter({ host: '127.0.0.1', port: server.address().port,
        password: 'synthetic-only', connectTimeoutMs: 1000, commandTimeoutMs: 1000 });
    // 测试结束幂等关闭 Redis 适配器。
    t.after(() => adapter.close());
    const operation = adapter.getRedis('chat:loop:missing');
    await connected;
    const started = performance.now();
    await adapter.close();
    assert.equal(await operation, null);
    assert.ok(performance.now() - started < 500);
    assert.equal(await adapter.setRedisExpire('chat:loop:missing', 'value', 1), false);
});

// V08-LOOP-03
// 验证连接拒绝后操作返回失败哨兵。
test('closed ephemeral loopback endpoint fails without retries', { timeout: 3000 }, async () => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const port = server.address().port;
    // 等待测试监听器完成关闭。
    await new Promise((resolve) => server.close(resolve));
    const adapter = createRedisAdapter({ host: '127.0.0.1', port, connectTimeoutMs: 100, commandTimeoutMs: 100 });
    try {
        const started = performance.now();
        assert.equal(await adapter.getRedis('chat:loop:missing'), null);
        assert.ok(performance.now() - started < 1000);
    } finally { await adapter.close(); }
});
