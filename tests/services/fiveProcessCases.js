'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { randomUUID, createHash } = require('node:crypto');
const { createRequire } = require('node:module');
const { reserve, stop } = require('./fourProcessCases');
const { poll, runCommand } = require('./dependencyCoordinator');
const { createTopology, nativeConfig, assertConnectedClients } = require('./twoServerTopology');
const { ClientControl } = require('./clientControl');
const { waitForConnectionCount } = require('./connectionCount');
const { SchemaMigration } = require('../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../schema/MysqlSession');
const fixture = require('./phase3d.fixture.json');
const requireVarify = createRequire(path.resolve(__dirname, '../../VarifyServer/package.json'));
const grpc = requireVarify('@grpc/grpc-js');
const loader = requireVarify('@grpc/proto-loader');

async function runFiveProcessCases(coordinator, record, evidenceRoot) {
    const bundle = process.env.CHAT_FOUR_BUNDLE;
    const clientBinary = process.env.CHAT_E2E_CLIENT;
    assert.ok(bundle && path.isAbsolute(bundle) && clientBinary && path.isAbsolute(clientBinary));
    const supervisor = path.join(bundle, 'driver', 'FourProcessDriver');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat3d-'));
    const children = [];
    const controls = [];
    const leases = {};
    const ports = {};
    const recipients = [];
    const users = [];
    const secrets = [coordinator.password];
    let topology;
    let sql;
    let primary;
    let step = 5;
    const test = (name, action) => record(`E03-CONTRACT-${String(++step).padStart(2, '0')}`, name, action);
    const env = { ...process.env, MYSQL_PWD: coordinator.password,
        CHAT_VARIFY_EMAIL_USER: 'fixture@example.invalid', CHAT_VARIFY_MYSQL_PASSWORD: coordinator.password,
        CHAT_VARIFY_REDIS_PASSWORD: coordinator.password };
    delete env.CHAT_CONFIG;
    delete env.LD_PRELOAD;
    function start(name, executable, args, extra = {}) {
        const report = path.join(root, `${name}-${children.length}.json`);
        const child = spawn(supervisor, ['supervise', executable, root, report, ...args], {
            env: { ...env, ...extra, LD_LIBRARY_PATH: `${path.dirname(supervisor)}:${path.dirname(executable)}` },
            windowsHide: true, stdio: ['ignore', 'ignore', 'ignore']
        });
        const owned = { name, child, report };
        owned.exited = new Promise(resolve => { child.once('error', () => resolve(-1)); child.once('exit', resolve); });
        children.push(owned);
        return owned;
    }
    async function release(...names) {
        for (const name of names) if (leases[name]) { await leases[name].release(); delete leases[name]; }
    }
    async function native(role) {
        const binary = role.startsWith('Chat') ? 'ChatServer' : role;
        const keys = { GateServer: ['gate'], StatusServer: ['status'], ChatA: ['chatA', 'rpcA'], ChatB: ['chatB', 'rpcB'] };
        await release(...keys[role]);
        const file = path.join(root, `${role}.ini`);
        fs.writeFileSync(file, nativeConfig(topology, role, { password: coordinator.password,
            redis: coordinator.config.ports.redis, mysql: coordinator.config.ports.mysql }, path.join(root, 'logs')), { mode: 0o600 });
        return start(role, path.join(bundle, binary, binary), ['--config', file]);
    }
    async function rpc(service, method, request) {
        const definition = grpc.loadPackageDefinition(loader.loadSync(path.resolve(__dirname, '../../proto',
            service === 'StatusService' ? 'status.proto' : 'varify.proto')));
        const client = new definition.message[service](`127.0.0.1:${service === 'StatusService' ? ports.status : ports.varify}`,
            grpc.credentials.createInsecure());
        try {
            return await new Promise((resolve, reject) => client[method](request, { deadline: Date.now() + 1500 },
                (error, result) => error ? reject(error) : resolve(result)));
        } finally { client.close(); }
    }
    async function client(name) {
        const control = new ClientControl(root, name);
        controls.push(control);
        await control.listen();
        const owned = start(name, clientBinary, ['--control', control.endpoint]);
        const ready = await control.ready();
        return { control, owned, pid: ready.pid };
    }
    async function count(name, expected) {
        await waitForConnectionCount(coordinator, name, expected, controls.filter(control => !control.failed));
    }
    const gate = () => `http://127.0.0.1:${ports.gate}`;
    async function authenticate(instance, user, register = true) {
        if (register) {
            recipients.push(user.email);
            assert.equal((await instance.control.command('verify', { gate: gate(), email: user.email })).error, 0);
            let code;
            await poll(async () => {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${user.email}`)}`);
                if (!found.messages.length) return false;
                const mail = await coordinator.mailApi(`/api/v1/message/${found.messages[0].ID}`);
                // Same production email format used by the accepted four-process fixture.
                code = /验证码为([a-zA-Z0-9-]+)请/.exec(mail.Text)?.[1];
                return Boolean(code);
            }, 5000);
            secrets.push(code);
            assert.equal((await instance.control.command('register', { gate: gate(), email: user.email,
                name: user.name, password: user.password, code })).error, 0);
        }
        const result = await instance.control.command('login', { gate: gate(), email: user.email, password: user.password });
        assert.equal(result.status, 'authenticated');
        user.uid = result.uid;
        return { logical: user.logical, pid: instance.pid, ...result };
    }
    let alice, bob;
    try {
        for (const name of ['gate', 'status', 'varify', 'chatA', 'rpcA', 'chatB', 'rpcB']) {
            leases[name] = await reserve(); ports[name] = leases[name].port;
        }
        topology = createTopology(coordinator.config.runId, ports);
        sql = new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD', coordinator.config.ids.mysql,
            'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET', '--user=root', ...mysqlArgs], env);
        await test('fresh isolated application schema', async () => {
            await sql.execute(`CREATE DATABASE \`${topology.database}\``);
            await new SchemaMigration(sql, topology.database).Apply();
        });
        await test('five native service protocol readiness', async () => {
            await release('varify');
            const file = path.join(root, 'varify.json');
            fs.writeFileSync(file, JSON.stringify({ email: { host: '127.0.0.1', port: coordinator.config.ports.smtp,
                secure: false, auth: 'none', deadlineMs: 2000 }, mysql: { host: '127.0.0.1', port: coordinator.config.ports.mysql },
                redis: { host: '127.0.0.1', port: coordinator.config.ports.redis } }), { mode: 0o600 });
            start('VarifyServer', process.execPath, [path.resolve(__dirname, '../../VarifyServer/server.js'), '--config', file],
                { CHAT_VARIFY_BIND_ADDRESS: `127.0.0.1:${ports.varify}` });
            const email = `ready-${topology.runId}@example.invalid`; recipients.push(email);
            await poll(async () => Number((await rpc('VarifyService', 'GetVarifyCode', { email })).error) === 0, 30000);
            await native('StatusServer');
            await poll(async () => Number((await rpc('StatusService', 'Login', { uid: -1, token: 'invalid' })).error) > 0, 30000);
            for (const role of ['ChatA', 'ChatB']) {
                await native(role);
                const port = role === 'ChatA' ? ports.chatA : ports.chatB;
                await poll(async () => {
                    const value = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 10000,
                        env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor),
                            CHAT_FOUR_WIRE: JSON.stringify({ port, login: { uid: -1, token: 'invalid' } }) } }));
                    return Number.isInteger(value.error) && value.error !== 0;
                }, 30000);
            }
            await native('GateServer');
            await poll(async () => (await fetch(`${gate()}/get_test`, { signal: AbortSignal.timeout(1500) })).status === 200, 30000);
        });
        await test('two live production client processes', async () => {
            alice = await client('alice'); bob = await client('bob');
            assert.notEqual(alice.pid, bob.pid);
            for (const instance of [alice, bob]) assert.equal((await instance.control.command('snapshot')).active, false);
        });
        await test('public registration and real distinct Status discovery', async () => {
            for (const user of topology.users) {
                const password = randomUUID().replaceAll('-', '').slice(0, 12);
                users.push({ ...user, password }); secrets.push(password);
            }
            const first = await authenticate(alice, users[0]);
            await count(topology.servers[0].name, 1);
            const second = await authenticate(bob, users[1]);
            await count(topology.servers[1].name, 1);
            assertConnectedClients(topology, [first, second]);
            topology.clients = [first, second];
        });
        let chatId;
        const uuid = randomUUID();
        await test('production models correlate a durable cross-instance message', async () => {
            const created = await alice.control.command('create', { toUid: users[1].uid });
            assert.equal(created.error, 0); chatId = created.chatId; assert.ok(chatId > 0);
            assert.equal((await alice.control.command('send', { toUid: users[1].uid, chatId, uuid, text: fixture.text })).error, 0);
            const expectedHash = createHash('sha256').update(fixture.text).digest('hex');
            let first, second;
            await poll(async () => {
                first = (await alice.control.command('snapshot', { chatId })).messages;
                second = (await bob.control.command('snapshot', { chatId })).messages;
                return first.length === 1 && second.length === 1 && BigInt(first[0].messageId) > 0;
            }, 10000);
            assert.equal(first[0].messageId, second[0].messageId);
            assert.equal(first[0].uuid, uuid); assert.equal(second[0].uuid, uuid);
            assert.equal(first[0].sha256, expectedHash); assert.equal(second[0].sha256, expectedHash);
            await sql.execute(`USE \`${topology.database}\``);
            assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
            assert.equal(await sql.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`), first[0].messageId);
        });
        await test('one client exits while the peer retains its independent session', async () => {
            assert.equal((await alice.control.command('stop')).status, 'stopped');
            await stop(alice.owned);
            await alice.control.close();
            await count(topology.servers[0].name, 0);
            const remaining = await bob.control.command('snapshot');
            assert.equal(remaining.active, true); assert.equal(remaining.uid, users[1].uid);
            assert.equal((await bob.control.command('snapshot', { chatId })).messages.length, 1);
        });
    } catch (error) { primary = error; }
    finally {
        const failures = [];
        for (const control of [...controls].reverse()) {
            try { if (!control.failed) assert.equal((await control.command('stop')).status, 'stopped'); }
            catch { failures.push('client-stop'); }
            try { await control.close(); } catch { failures.push('control-close'); }
        }
        for (const owned of [...children].reverse()) {
            try { await stop(owned); } catch { failures.push(`stop-${owned.name}`); }
        }
        for (const lease of Object.values(leases)) {
            try { await lease.release(); } catch { failures.push('lease-close'); }
        }
        try {
            const redis = await coordinator.redis();
            try {
                const keys = recipients.map(email => `code_${email}`);
                for (const user of users.filter(user => user.uid)) {
                    for (const prefix of ['utoken_', 'uip_', 'ubaseinfo_', 'usessionid_', 'lock_']) keys.push(`${prefix}${user.uid}`);
                }
                if (keys.length) { await redis.del(...keys); assert.equal(await redis.exists(...keys), 0); }
                if (topology) for (const server of topology.servers) assert.equal(await redis.hexists('logincount', server.name), 0);
            } finally { redis.disconnect(); }
            for (const email of recipients) {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`);
                if (found.messages.length) await coordinator.mailApi('/api/v1/messages', { method: 'DELETE',
                    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ IDs: found.messages.map(value => value.ID) }) });
                assert.equal((await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`)).messages.length, 0);
            }
            if (sql && topology) {
                await sql.execute(`DROP DATABASE IF EXISTS \`${topology.database}\``);
                assert.equal(await sql.execute(`SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${topology.database}'`), '0');
            }
        } catch { failures.push('application-data-cleanup'); }
        if (sql) try { await sql.close(); } catch { failures.push('mysql-close'); }
        if (!failures.length) fs.rmSync(root, { recursive: true });
        const evidence = { topology: topology || { status: 'not-started' }, complete: failures.length === 0, failures };
        const encoded = JSON.stringify(evidence);
        const redacted = secrets.every(secret => !encoded.includes(secret));
        fs.writeFileSync(path.join(evidenceRoot, 'topology.json'), JSON.stringify(evidence.topology, null, 2));
        fs.writeFileSync(path.join(evidenceRoot, 'application-teardown.json'), JSON.stringify({ complete: evidence.complete, failures }));
        fs.writeFileSync(path.join(evidenceRoot, 'redaction.json'), JSON.stringify({ complete: redacted }));
        try {
            await record('E03-CONTRACT-12', 'reverse client service data and control cleanup', async () => {
                assert.deepEqual(failures, []); assert.equal(redacted, true);
            });
        } catch (error) { primary ||= error; }
    }
    if (primary) throw primary;
}

module.exports = { runFiveProcessCases };
