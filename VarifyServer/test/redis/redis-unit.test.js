'use strict';

const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const test = require('node:test');
const { createRedisAdapter, normalizeRedisConfig } = require('../../redis');

function fixture() {
    const clients = [];
    class Client extends EventEmitter {
        constructor(options) { super(); this.options = options; this.status = 'wait'; this.calls = []; clients.push(this); }
        async connect() { this.status = 'ready'; }
        async get() { return 'value'; }
        async exists() { return 1; }
        async set(...args) { this.calls.push(args); return 'OK'; }
        disconnect() { this.status = 'end'; }
    }
    return { clients, Client, adapter: createRedisAdapter({ host: '127.0.0.1', port: 6379 }, { Redis: Client }) };
}

// V08-UNT-01
test('Redis module and factory do not connect before the first operation', async () => {
    const { adapter, clients } = fixture();
    assert.equal(clients.length, 0);
    assert.equal(await adapter.GetRedis('key'), 'value');
    assert.equal(clients.length, 1);
    assert.equal(clients[0].options.lazyConnect, true);
    assert.equal(clients[0].options.retryStrategy, null);
    assert.equal(clients[0].options.autoResendUnfulfilledCommands, false);
    assert.equal(clients[0].options.enableOfflineQueue, false);
    await adapter.Quit();
});

// V08-UNT-02
test('verification write is one atomic SET EX command', async () => {
    const { adapter, clients } = fixture();
    assert.equal(await adapter.setRedisExpire('key', 'value', 600), true);
    assert.equal(await adapter.setRedisExpire('key', 'value', Number.MAX_SAFE_INTEGER), true);
    assert.deepEqual(clients[0].calls, [
        ['key', 'value', 'EX', 600], ['key', 'value', 'EX', Number.MAX_SAFE_INTEGER]
    ]);
    await adapter.Quit();
});

// V08-UNT-03
test('concurrent first operations share one connecting client', async () => {
    const { adapter, clients } = fixture();
    assert.deepEqual(await Promise.all([adapter.GetRedis('a'), adapter.QueryRedis('b')]), ['value', 1]);
    assert.equal(clients.length, 1);
    await adapter.Quit();
});

// V08-UNT-04
test('failure preserves null/false mapping and next call replaces the failed client', async () => {
    const { adapter, clients } = fixture();
    await adapter.GetRedis('key');
    clients[0].get = async () => { throw new Error('synthetic-secret'); };
    assert.equal(await adapter.GetRedis('key'), null);
    assert.equal(clients[0].status, 'end');
    assert.equal(await adapter.GetRedis('key'), 'value');
    assert.equal(clients.length, 2);
    clients[1].set = async () => { throw new Error('synthetic-secret'); };
    assert.equal(await adapter.setRedisExpire('key', 'value', 600), false);
    await adapter.Quit();
});

// V08-UNT-05
test('close is idempotent and blocks future connections and writes', async () => {
    const { adapter, clients } = fixture();
    await adapter.GetRedis('key');
    await adapter.Quit();
    await adapter.Quit();
    assert.equal(clients[0].status, 'end');
    assert.equal(await adapter.GetRedis('key'), null);
    assert.equal(await adapter.setRedisExpire('key', 'value', 600), false);
    assert.equal(clients.length, 1);
});

// V08-UNT-06
test('invalid deadlines and TTL fail before network activity', async () => {
    for (const value of [0, -1, Infinity, '1000', 60001]) {
        assert.throws(() => normalizeRedisConfig({ host: '127.0.0.1', port: 6379, commandTimeoutMs: value }), /Redis/);
    }
    const { adapter, clients } = fixture();
    for (const ttl of [0, -1, 1.2, Infinity, '600', Number.MAX_SAFE_INTEGER + 1]) {
        assert.equal(await adapter.setRedisExpire('key', 'value', ttl), false);
    }
    assert.equal(clients.length, 0);
    await adapter.Quit();
});
