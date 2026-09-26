'use strict';

const assert = require('node:assert/strict');
const fixture = require('./phase3d.fixture.json');

const portNames = ['gate', 'status', 'varify', 'chatA', 'rpcA', 'chatB', 'rpcB'];

/** 校验运行身份和独立端口，生成双 Chat 实例及测试用户的隔离拓扑。 */
function createTopology(runId, ports) {
    assert.match(runId, /^[a-f0-9]{32}$/);
    assert.deepEqual(Object.keys(ports).sort(), [...portNames].sort());
    for (const port of Object.values(ports)) assert.ok(Number.isInteger(port) && port > 0 && port <= 65535);
    assert.equal(new Set(Object.values(ports)).size, portNames.length, 'distinct application endpoints required');
    return {
        format: fixture.format, fixture: fixture.identity, seed: fixture.seed, runId,
        database: `chat_${runId}_3d`, host: '127.0.0.1', ports: { ...ports },
        servers: fixture.servers.map(/** 为逻辑实例生成带运行身份的名称及独立 TCP、RPC 端口。 */ (logical, index) => ({ logical, name: `${logical}-${runId}`,
            port: index === 0 ? ports.chatA : ports.chatB, rpcPort: index === 0 ? ports.rpcA : ports.rpcB })),
        users: fixture.users.map(/** 为逻辑用户生成本次唯一名称和不可投递的测试邮箱。 */ logical => ({ logical, name: `${logical}_${runId}`,
            email: `${logical}-${runId}@example.invalid` }))
    };
}

/** 按角色生成生产 INI，拒绝非法凭据、路径及重叠依赖端口。 */
function nativeConfig(topology, role, dependencies, logDirectory) {
    assert.ok(['GateServer', 'StatusServer', 'ChatA', 'ChatB'].includes(role));
    // Only a run-generated credential and single-line owned path can enter INI.
    assert.match(dependencies.password, /^[a-f0-9]{64}$/);
    for (const key of ['redis', 'mysql']) {
        assert.ok(Number.isInteger(dependencies[key]) && dependencies[key] > 0 && dependencies[key] <= 65535);
        assert.ok(!Object.values(topology.ports).includes(dependencies[key]));
    }
    assert.notEqual(dependencies.redis, dependencies.mysql);
    assert.ok(typeof logDirectory === 'string' && logDirectory.length > 0 && !/[\r\n\0]/.test(logDirectory));
    const section = /** 把单个配置节与键值序列编码为 INI 文本。 */ (name, values) => `[${name}]\n` + Object.entries(values).map(/** 把已校验的单个键值编码为配置行。 */ ([key, value]) => `${key}=${value}\n`).join('');
    let result = section('Redis', { Host: topology.host, Port: dependencies.redis, Password: dependencies.password }) +
        (role === 'StatusServer' ? '' : section('Mysql', { Host: topology.host, Port: dependencies.mysql, User: 'root',
            Password: dependencies.password, Schema: topology.database })) +
        section('StatusServer', { Host: topology.host, Port: topology.ports.status }) +
        section('Log', { Name: role, LogDir: logDirectory, MaxSizeMB: 1, MaxTotalFiles: 2, Level: 'warn', FlushLevel: 'warn' });
    if (role === 'ChatA' || role === 'ChatB') {
        const index = role === 'ChatA' ? 0 : 1;
        const self = topology.servers[index];
        const peer = topology.servers[1 - index];
        result += section('SelfServer', { Name: self.name, Host: topology.host, Port: self.port, RPCPort: self.rpcPort }) +
            section('PeerServer', { Servers: 'Peer' }) +
            section('Peer', { Name: peer.name, Host: topology.host, Port: peer.rpcPort });
    } else {
        result += section('GateServer', { Port: topology.ports.gate }) +
            section('VarifyServer', { Host: topology.host, Port: topology.ports.varify }) +
            section('ChatServers', { Name: 'ChatA,ChatB' });
        topology.servers.forEach(/** 把两个 Chat 实例的选服端点加入配置。 */ (server, index) => {
            result += section(index === 0 ? 'ChatA' : 'ChatB', { Name: server.name, Host: topology.host, Port: server.port });
        });
    }
    return result;
}

/** 核对两个已观察客户端的用户、PID 和实例端点，拒绝重复或错连。 */
function assertConnectedClients(topology, clients) {
    assert.equal(clients.length, 2, 'two observed clients required');
    const pids = new Set();
    const users = new Set();
    clients.forEach(/** 核对单个客户端与预期逻辑用户及实例的对应关系。 */ (client, index) => {
        assert.equal(client.logical, fixture.users[index]);
        assert.ok(Number.isSafeInteger(client.pid) && client.pid > 0);
        assert.ok(Number.isSafeInteger(client.uid) && client.uid > 0);
        assert.equal(client.active, true);
        assert.equal(client.host, topology.host);
        assert.equal(client.port, topology.servers[index].port, 'discovered endpoint must match its distinct instance');
        pids.add(client.pid);
        users.add(client.uid);
    });
    assert.equal(pids.size, 2, 'independent client processes required');
    assert.equal(users.size, 2, 'independent authenticated accounts required');
}

module.exports = { createTopology, nativeConfig, assertConnectedClients };
