'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const { createTopology, nativeConfig, assertConnectedClients } = require('./twoServerTopology');
const ports = { gate: 31001, status: 31002, varify: 31003, chatA: 31004, rpcA: 31005, chatB: 31006, rpcB: 31007 };
const runId = 'a'.repeat(32);

test('fixed fixture preserves logical identities while namespacing physical resources', () => {
    const first = createTopology(runId, ports);
    const second = createTopology('b'.repeat(32), ports);
    assert.equal(first.fixture, 'phase3d-2026-08-30');
    assert.equal(first.seed, '0x3D20260830');
    assert.deepEqual(first.servers.map(server => server.logical), ['chat-e2e-a', 'chat-e2e-b']);
    assert.deepEqual(first.users.map(user => user.logical), ['alice', 'bob']);
    assert.notEqual(first.database, second.database);
    assert.notEqual(first.users[0].email, second.users[0].email);
    assert.throws(() => createTopology('../outside', ports));
    assert.throws(() => createTopology(runId, { ...ports, chatB: ports.chatA }));
    assert.throws(() => createTopology(runId, { ...ports, rpcB: 0 }));
});

test('native configs use reciprocal peer RPC ports and both discoverable client endpoints', () => {
    const topology = createTopology(runId, ports);
    const dependencies = { password: 'c'.repeat(64), redis: 32001, mysql: 32002 };
    const first = nativeConfig(topology, 'ChatA', dependencies, '/owned/logs');
    const second = nativeConfig(topology, 'ChatB', dependencies, '/owned/logs');
    const status = nativeConfig(topology, 'StatusServer', dependencies, '/owned/logs');
    assert.match(first, /Port=31004\nRPCPort=31005/);
    assert.match(first, /\[Peer\]\nName=chat-e2e-b-[a-f0-9]+\nHost=127.0.0.1\nPort=31007/);
    assert.match(second, /\[Peer\]\nName=chat-e2e-a-[a-f0-9]+\nHost=127.0.0.1\nPort=31005/);
    assert.match(status, /\[ChatServers\]\nName=ChatA,ChatB/);
    assert.match(status, /Port=31004/);
    assert.match(status, /Port=31006/);
    assert.ok(!JSON.stringify(topology).includes(dependencies.password));
    assert.throws(() => nativeConfig(topology, 'ChatA', dependencies, '/owned\nPassword=bad'));
});

test('missing second instance, shared process/account and wrong discovery cannot pass topology', () => {
    const topology = createTopology(runId, ports);
    const clients = [
        { logical: 'alice', pid: 101, uid: 11, active: true, host: '127.0.0.1', port: ports.chatA },
        { logical: 'bob', pid: 102, uid: 12, active: true, host: '127.0.0.1', port: ports.chatB }
    ];
    assertConnectedClients(topology, clients);
    assert.throws(() => assertConnectedClients(topology, clients.slice(0, 1)));
    for (const change of [{ pid: 101 }, { uid: 11 }, { port: ports.chatA }, { active: false }]) {
        assert.throws(() => assertConnectedClients(topology, [clients[0], { ...clients[1], ...change }]));
    }
});
