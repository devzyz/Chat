'use strict';

const assert = require('node:assert/strict');
const fixture = require('./phase3d.fixture.json');

const portNames = ['gate', 'status', 'varify', 'chatA', 'rpcA', 'chatB', 'rpcB'];

function createTopology(runId, ports) {
    assert.match(runId, /^[a-f0-9]{32}$/);
    assert.deepEqual(Object.keys(ports).sort(), [...portNames].sort());
    for (const port of Object.values(ports)) assert.ok(Number.isInteger(port) && port > 0 && port <= 65535);
    assert.equal(new Set(Object.values(ports)).size, portNames.length, 'distinct application endpoints required');
    return {
        format: fixture.format, fixture: fixture.identity, seed: fixture.seed, runId,
        database: `chat_${runId}_3d`, host: '127.0.0.1', ports: { ...ports },
        servers: fixture.servers.map((logical, index) => ({ logical, name: `${logical}-${runId}`,
            port: index === 0 ? ports.chatA : ports.chatB, rpcPort: index === 0 ? ports.rpcA : ports.rpcB })),
        users: fixture.users.map(logical => ({ logical, name: `${logical}_${runId}`,
            email: `${logical}-${runId}@example.invalid` }))
    };
}

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
    const section = (name, values) => `[${name}]\n` + Object.entries(values).map(([key, value]) => `${key}=${value}\n`).join('');
    let result = section('Redis', { Host: topology.host, Port: dependencies.redis, Password: dependencies.password }) +
        section('Mysql', { Host: topology.host, Port: dependencies.mysql, User: 'root',
            Password: dependencies.password, Schema: topology.database }) +
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
        topology.servers.forEach((server, index) => {
            result += section(index === 0 ? 'ChatA' : 'ChatB', { Name: server.name, Host: topology.host, Port: server.port });
        });
    }
    return result;
}

function assertConnectedClients(topology, clients) {
    assert.equal(clients.length, 2, 'two observed clients required');
    const pids = new Set();
    const users = new Set();
    clients.forEach((client, index) => {
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
