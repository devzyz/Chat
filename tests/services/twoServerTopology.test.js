'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const { createTopology, nativeConfig, assertConnectedClients } = require('./twoServerTopology');
const ports = { gate: 31001, varify: 31003, chatA: 31004, rpcA: 31005, chatB: 31006, rpcB: 31007 };
const runId = 'a'.repeat(32);

// 验证拓扑保留固定夹具身份，但每次运行的数据库和用户隔离且端口合法。
test('fixed fixture preserves logical identities while namespacing physical resources', () => {
    const first = createTopology(runId, ports);
    const second = createTopology('b'.repeat(32), ports);
    assert.equal(first.fixture, 'phase3d-2026-08-30');
    assert.equal(first.seed, '0x3D20260830');
    // 提取实例逻辑名以核对夹具顺序。
    assert.deepEqual(first.servers.map(server => server.logical), ['chat-e2e-a', 'chat-e2e-b']);
    // 提取用户逻辑名以核对夹具身份。
    assert.deepEqual(first.users.map(user => user.logical), ['alice', 'bob']);
    assert.notEqual(first.database, second.database);
    assert.notEqual(first.users[0].email, second.users[0].email);
    // 构造越界路径式运行标识，验证其被拒绝。
    assert.throws(() => createTopology('../outside', ports));
    // 构造重复 Chat 端口，验证实例不能共用监听端点。
    assert.throws(() => createTopology(runId, { ...ports, chatB: ports.chatA }));
    // 构造零 RPC 端口，验证端口范围检查。
    assert.throws(() => createTopology(runId, { ...ports, rpcB: 0 }));
});

// 验证双实例互为 RPC 对端且 Gate 选服配置一致，拒绝路径换行注入。
test('native configs use reciprocal peer RPC ports and both discoverable client endpoints', () => {
    const topology = createTopology(runId, ports);
    const dependencies = { password: 'c'.repeat(64), redis: 32001, mysql: 32002 };
    const first = nativeConfig(topology, 'ChatA', dependencies, '/owned/logs');
    const second = nativeConfig(topology, 'ChatB', dependencies, '/owned/logs');
    const gate = nativeConfig(topology, 'GateServer', dependencies, '/owned/logs');
    assert.match(first, /Port=31004\nRPCPort=31005/);
    assert.match(first, /\[Peer\]\nName=chat-e2e-b-[a-f0-9]+\nHost=127.0.0.1\nPort=31007/);
    assert.match(second, /\[Peer\]\nName=chat-e2e-a-[a-f0-9]+\nHost=127.0.0.1\nPort=31005/);
    assert.match(gate, /\[ChatServers\]\nName=ChatA,ChatB/);
    assert.doesNotMatch(gate, /\[StatusServer\]/);
    assert.match(first, /\[Mysql\]/);
    assert.match(second, /\[Mysql\]/);
    assert.match(gate, /Port=31004/);
    assert.match(gate, /Port=31006/);
    assert.ok(!JSON.stringify(topology).includes(dependencies.password));
    // 在日志路径中注入换行，验证不能生成额外配置项。
    assert.throws(() => nativeConfig(topology, 'ChatA', dependencies, '/owned\nPassword=bad'));
});

// 验证观察到的两客户端身份与实例对应，拒绝重复、缺项及未激活状态。
test('missing second instance, shared process/account and wrong discovery cannot pass topology', () => {
    const topology = createTopology(runId, ports);
    const clients = [
        { logical: 'alice', pid: 101, uid: 11, active: true, host: '127.0.0.1', port: ports.chatA },
        { logical: 'bob', pid: 102, uid: 12, active: true, host: '127.0.0.1', port: ports.chatB }
    ];
    assertConnectedClients(topology, clients);
    // 构造缺少一个客户端的观察结果。
    assert.throws(() => assertConnectedClients(topology, clients.slice(0, 1)));
    for (const change of [{ pid: 101 }, { uid: 11 }, { port: ports.chatA }, { active: false }]) {
        // 逐项注入重复身份、错误端点或未激活状态。
        assert.throws(() => assertConnectedClients(topology, [clients[0], { ...clients[1], ...change }]));
    }
});
