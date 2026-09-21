'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { randomUUID } = require('node:crypto');
const { createRequire } = require('node:module');
const { SchemaMigration } = require('../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../schema/MysqlSession');
const requireVarify = createRequire(path.resolve(__dirname, '../../VarifyServer/package.json'));
const grpc = requireVarify('@grpc/grpc-js');
const loader = requireVarify('@grpc/proto-loader');

async function reserve() {
    const server = net.createServer();
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    return { port: server.address().port, release: () => new Promise(resolve => server.close(resolve)) };
}

async function stop(owned, expected = 0) {
    if (owned.stopped) return;
    const fail = category => { throw new Error(`FourProcess:${owned.name}:${category}`); };
    owned.child.kill('SIGTERM');
    let timer;
    const result = await Promise.race([owned.exited, new Promise(resolve => { timer = setTimeout(() => resolve('timeout'), 15000); })]);
    clearTimeout(timer);
    if (result === 'timeout') fail('stop-timeout');
    let report;
    try { report = JSON.parse(fs.readFileSync(owned.report)); }
    catch { fail('report-unavailable'); }
    if (report.escalated !== false) fail('stop-escalated');
    if (Number.isSafeInteger(report.exitCode) && report.exitCode !== 0 && expected === 0) {
        fail(`exit-${report.exitCode}`);
    }
    if (result !== 0 || report.complete !== true) fail('harness-incomplete');
    if (expected === 0 && report.exitCode !== 0) fail('exit-unavailable');
    if (expected !== 0 && (!Number.isSafeInteger(report.exitCode) || report.exitCode === 0)) fail('expected-failure');
    owned.stopped = true;
}

async function runFourProcessCases(coordinator, record) {
    const { poll, runCommand } = require('./dependencyCoordinator');
    const bundle = process.env.CHAT_FOUR_BUNDLE;
    assert.ok(bundle && path.isAbsolute(bundle), 'same-run four-process bundle required');
    const driver = path.join(bundle, 'driver', 'FourProcessDriver');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), `chat-four-${coordinator.config.runId}-`));
    const children = [];
    const leases = {};
    const ports = {};
    const users = [];
    const recipients = [];
    const database = `${coordinator.config.database}_four`;
    const environment = { ...process.env, MYSQL_PWD: coordinator.password,
        CHAT_VARIFY_EMAIL_USER: 'four@example.invalid', CHAT_VARIFY_MYSQL_PASSWORD: coordinator.password,
        CHAT_VARIFY_REDIS_PASSWORD: coordinator.password };
    delete environment.LD_PRELOAD;
    delete environment.CHAT_CONFIG;
    const session = new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD', coordinator.config.ids.mysql,
        'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET', '--user=root', ...mysqlArgs], environment);
    const runName = `four_${coordinator.config.runId}`;
    let primary;
    let counter = 0;
    const test = (name, action) => record(`T10-4PROC-${String(++counter).padStart(2, '0')}`, name, action);
    async function start(name, executable, args, env = environment) {
        const report = path.join(root, `${name}-${children.length}.json`);
        const child = spawn(driver, ['supervise', executable, root, report, ...args], {
            env: { ...env, LD_LIBRARY_PATH: `${path.dirname(driver)}:${path.dirname(executable)}` }, windowsHide: true,
            stdio: ['ignore', 'ignore', 'ignore']
        });
        const owned = { child, report, name };
        owned.exited = new Promise(resolve => { child.once('error', () => resolve(-1)); child.once('exit', resolve); });
        children.push(owned);
        return owned;
    }
    function writeConfig(name) {
        const common = `[Redis]\nHost=127.0.0.1\nPort=${coordinator.config.ports.redis}\nPassword=${coordinator.password}\n` +
            `[Mysql]\nHost=127.0.0.1\nPort=${coordinator.config.ports.mysql}\nUser=root\nPassword=${coordinator.password}\nSchema=${database}\n` +
            `[StatusServer]\nHost=127.0.0.1\nPort=${ports.StatusServer}\n` +
            `[Log]\nName=${name}\nLogDir=${root}/logs\nMaxSizeMB=1\nMaxTotalFiles=2\nLevel=warn\nFlushLevel=warn\n`;
        const extra = name === 'ChatServer'
            ? `[SelfServer]\nName=${runName}\nHost=127.0.0.1\nPort=${ports.ChatServer}\nRPCPort=${ports.ChatRPC}\n[PeerServer]\nServers=\n`
            : `[GateServer]\nPort=${ports.GateServer}\n[VarifyServer]\nHost=127.0.0.1\nPort=${ports.VarifyServer}\n` +
              `[ChatServers]\nName=${runName}\n[${runName}]\nName=${runName}\nHost=127.0.0.1\nPort=${ports.ChatServer}\n`;
        const filename = path.join(root, `${name}.ini`);
        fs.writeFileSync(filename, common + extra, { mode: 0o600 });
        return filename;
    }
    async function native(name) {
        if (leases[name]) { await leases[name].release(); delete leases[name]; }
        if (name === 'ChatServer' && leases.ChatRPC) { await leases.ChatRPC.release(); delete leases.ChatRPC; }
        return start(name, path.join(bundle, name, name), ['--config', writeConfig(name)]);
    }
    async function varify() {
        if (leases.VarifyServer) { await leases.VarifyServer.release(); delete leases.VarifyServer; }
        const config = path.join(root, 'varify.json');
        fs.writeFileSync(config, JSON.stringify({ email: { host: '127.0.0.1', port: coordinator.config.ports.smtp,
            secure: false, auth: 'none', deadlineMs: 2000 }, mysql: { host: '127.0.0.1', port: coordinator.config.ports.mysql },
            redis: { host: '127.0.0.1', port: coordinator.config.ports.redis } }));
        return start('VarifyServer', process.execPath, [path.resolve(__dirname, '../../VarifyServer/server.js'), '--config', config],
            { ...environment, CHAT_VARIFY_BIND_ADDRESS: `127.0.0.1:${ports.VarifyServer}` });
    }
    async function http(route, body) {
        const response = await fetch(`http://127.0.0.1:${ports.GateServer}${route}`, {
            method: body ? 'POST' : 'GET', body: body ? JSON.stringify(body) : undefined,
            headers: { 'Content-Type': 'application/json' }, signal: AbortSignal.timeout(10000)
        });
        assert.equal(response.status, 200);
        return body ? response.json() : response.text();
    }
    async function wire(login, requests = []) {
        return JSON.parse(await runCommand(driver, ['chat'], {
            env: { ...environment, LD_LIBRARY_PATH: path.dirname(driver),
                CHAT_FOUR_WIRE: JSON.stringify({ port: ports.ChatServer, login, requests }) }, timeout: 20000
        }));
    }
    async function rpcReady(service, method, request) {
        const file = service === 'StatusService' ? 'status.proto' : 'varify.proto';
        const definition = grpc.loadPackageDefinition(loader.loadSync(path.resolve(__dirname, '../../proto', file)));
        const client = new definition.message[service](`127.0.0.1:${ports[service === 'StatusService' ? 'StatusServer' : 'VarifyServer']}`, grpc.credentials.createInsecure());
        try {
            return await new Promise((resolve, reject) => client[method](request, { deadline: Date.now() + 2000 }, (error, value) => error ? reject(error) : resolve(value)));
        } finally { client.close(); }
    }
    async function register(index) {
        const email = `${coordinator.config.runId}-four-${index}@example.invalid`;
        recipients.push(email);
        assert.equal((await http('/get_varifycode', { email })).error, 0);
        let code;
        await poll(async () => {
            const result = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`);
            if (!result.messages.length) return false;
            const mail = await coordinator.mailApi(`/api/v1/message/${result.messages[0].ID}`);
            code = /验证码为([a-zA-Z0-9-]+)请/.exec(mail.Text)?.[1];
            return Boolean(code);
        }, 5000);
        const password = randomUUID();
        assert.equal((await http('/user_register', { user: `four_${index}`, email, passwd: password,
            confirm: password, varifycode: code })).error, 0);
        const login = await http('/user_login', { email, password });
        assert.equal(login.error, 0);
        assert.equal(Number(login.port), ports.ChatServer);
        users.push({ uid: login.uid, token: login.token, email, password });
    }
    try {
        for (const name of ['VarifyServer', 'StatusServer', 'ChatServer', 'ChatRPC', 'GateServer']) {
            leases[name] = await reserve(); ports[name] = leases[name].port;
        }
        await test('fresh application schema', async () => {
            await session.execute(`CREATE DATABASE \`${database}\``);
            await new SchemaMigration(session, database).Apply();
        });
        await test('Varify production gRPC ready', async () => {
            await varify();
            const email = `${coordinator.config.runId}-four-ready@example.invalid`; recipients.push(email);
            await poll(async () => (await rpcReady('VarifyService', 'GetVarifyCode', { email })).error === 0, 30000);
        });
        await test('Status production gRPC ready', async () => {
            await native('StatusServer');
            await poll(async () => Number((await rpcReady('StatusService', 'Login', { uid: -1, token: 'invalid' })).error) !== 0, 30000);
        });
        await test('Chat production TCP ready', async () => {
            await native('ChatServer');
            await poll(async () => (await wire({ uid: -1, token: 'invalid' })).error !== 0, 30000);
        });
        await test('Gate production HTTP ready', async () => { await native('GateServer'); await poll(async () => Boolean(await http('/get_test')), 30000); });
        await test('mail registration login and server selection', async () => { await register(1); await register(2); });
        let chat;
        await test('TCP authentication and public private-chat creation', async () => {
            const result = await wire(users[0], [{ id: 1023, body: { self_id: users[0].uid, other_id: users[1].uid } }]);
            assert.equal(result.error, 0); assert.equal(result.responses[0].error, 0);
            chat = result.responses[0].chat_id; assert.ok(chat > 0);
        });
        const uuid = randomUUID();
        const message = () => ({ id: 1016, body: { from_uid: users[0].uid, to_uid: users[1].uid, chat_id: chat,
            text_array: [{ msg_uuid: uuid, msg_content: 'four-process synthetic message' }] } });
        await test('public message commit and durable UUID', async () => {
            const result = await wire(users[0], [message()]);
            assert.equal(result.responses[0].error, 0);
            // MysqlSession uses a private delimiter and accepts one statement;
            // the mysql client's USE command must not consume a following query.
            await session.execute(`USE \`${database}\``);
            assert.equal(await session.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
        });
        await test('disconnect retry preserves single durable message', async () => {
            const before = await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`);
            const result = await wire(users[0], [message()]); assert.equal(result.responses[0].error, 0);
            assert.equal(await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`), before);
            assert.equal(await session.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
        });
        await test('occupied Gate port fails without disturbing original', async () => {
            const conflict = await native('GateServer');
            await poll(async () => conflict.child.exitCode !== null, 15000);
            await stop(conflict, 1); assert.ok(await http('/get_test'));
        });
        await test('SMTP unavailable returns business failure within deadline', async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            const email = `${coordinator.config.runId}-four-unavailable@example.invalid`; recipients.push(email);
            try { assert.notEqual((await http('/get_varifycode', { email })).error, 0); }
            finally { await coordinator.lifecycle('mailpit', 'start'); }
            await poll(() => coordinator.mailApi('/readyz'), 5000);
        });
        await test('SMTP recovery uses refreshed mapping', async () => {
            await stop(children.find(value => value.name === 'VarifyServer' && !value.stopped));
            await varify();
            const email = `${coordinator.config.runId}-four-recovered@example.invalid`; recipients.push(email);
            await poll(async () => (await http('/get_varifycode', { email })).error === 0, 30000);
        });
        await test('Redis outage fails closed', async () => {
            await coordinator.lifecycle('redis', 'stop');
            try { assert.notEqual((await http('/user_login', { email: users[0].email, password: users[0].password })).error, 0); }
            finally {
                await coordinator.lifecycle('redis', 'start');
                await poll(async () => {
                    const client = await coordinator.redis(null);
                    try { await client.config('SET', 'requirepass', coordinator.password); return true; }
                    finally { client.disconnect(); }
                }, 5000);
            }
        });
        await test('application recovery after dependency port remap', async () => {
            for (const owned of [...children].reverse()) await stop(owned);
            await varify(); await native('StatusServer'); await native('ChatServer'); await native('GateServer');
            await poll(async () => {
                const login = await http('/user_login', { email: users[0].email, password: users[0].password });
                if (login.error !== 0) return false;
                users[0].token = login.token;
                return (await wire(users[0])).error === 0;
            }, 30000);
        });
        await test('recovered retry retains original durable ID', async () => {
            const before = await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`);
            assert.equal((await wire(users[0], [message()])).responses[0].error, 0);
            assert.equal(await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`), before);
        });
        await test('Redis stores current instance routing', async () => {
            const client = await coordinator.redis();
            try { await poll(async () => await client.hget('logincount', runName) === '0', 5000); }
            finally { client.disconnect(); }
        });
        await test('Gate shutdown interrupts a real blocked MySQL read', async () => {
            await session.execute('LOCK TABLES user WRITE');
            const pending = http('/user_login', { email: users[0].email, password: users[0].password }).catch(() => null);
            try {
                await poll(async () => Number(await coordinator.sql(
                    `SELECT COUNT(*) FROM information_schema.processlist WHERE DB='${database}' AND STATE LIKE '%lock%'`)) > 0, 1500);
                await stop(children.find(value => value.name === 'GateServer' && !value.stopped));
            } finally { await session.execute('UNLOCK TABLES'); await pending; }
        });
    } catch (error) { primary = error; }
    finally {
        const failures = [];
        for (const owned of children.reverse()) {
            try { await stop(owned); } catch { failures.push(`stop-${owned.name}`); }
        }
        for (const lease of Object.values(leases)) await lease.release();
        try {
            const client = await coordinator.redis();
            try {
                const keys = recipients.map(email => `code_${email}`);
                for (const user of users) for (const prefix of ['utoken_', 'uip_', 'ubaseinfo_', 'usessionid_', 'lock_']) keys.push(`${prefix}${user.uid}`);
                if (keys.length) { await client.del(...keys); assert.equal(await client.exists(...keys), 0); }
                assert.equal(await client.hexists('logincount', runName), 0);
            } finally { client.disconnect(); }
            for (const email of recipients) {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`);
                if (found.messages.length) await coordinator.mailApi('/api/v1/messages', { method: 'DELETE',
                    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ IDs: found.messages.map(value => value.ID) }) });
                assert.equal((await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`)).messages.length, 0);
            }
            await session.execute(`DROP DATABASE IF EXISTS \`${database}\``);
            assert.equal(await session.execute(`SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${database}'`), '0');
            for (const port of Object.values(ports)) {
                const listener = net.createServer();
                await new Promise((resolve, reject) => { listener.once('error', reject); listener.listen(port, '127.0.0.1', resolve); });
                await new Promise(resolve => listener.close(resolve));
            }
        } catch { failures.push('owned-data-cleanup'); }
        await session.close();
        if (failures.length === 0) fs.rmSync(root, { recursive: true });
        try { await record('T10-4PROC-18', 'reverse graceful process and data cleanup', async () => { assert.deepEqual(failures, []); }); }
        catch (error) { primary ||= error; }
    }
    if (primary) throw primary;
}

module.exports = { reserve, stop, runFourProcessCases };
