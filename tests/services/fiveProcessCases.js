'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { randomUUID, createHash } = require('node:crypto');
const { createRequire } = require('node:module');
const { reserve, stop } = require('./fourProcessCases');
const { poll, runCommand, contractStep, caseDiagnostic } = require('./dependencyCoordinator');
const { createTopology, nativeConfig, assertConnectedClients } = require('./twoServerTopology');
const { ClientControl } = require('./clientControl');
const { waitForConnectionCount } = require('./connectionCount');
const { SchemaMigration } = require('../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../schema/MysqlSession');
const fixture = require('./phase3d.fixture.json');
const requireVarify = createRequire(path.resolve(__dirname, '../../VarifyServer/package.json'));
const grpc = requireVarify('@grpc/grpc-js');
const loader = requireVarify('@grpc/proto-loader');
const { runFriendshipCases } = require('./friendshipCases');
const { runMessagingCases, runOfflineMessageCase } = require('./messagingCases');
const { runHistoryRecoveryCases } = require('./historyRecoveryCases');
const { runFaultRecoveryCases } = require('./faultRecoveryCases');
const { realAcceptanceEnabled, runFirstReleaseCases } = require('./firstReleaseCases');

/** 在所属临时环境组装双 Chat 及真实 Qt 客户端，按选择器验证业务并清理全部资源。 */
async function runFiveProcessCases(coordinator, record, evidenceRoot, selector = '3D-00') {
    const realAcceptance = realAcceptanceEnabled(selector);
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
    const auxiliaryUsers = [];
    const servers = {};
    const secrets = [coordinator.password];
    let topology;
    let sql;
    let primary;
    let step = 5;
    // 为基础五进程合同分配连续 Test ID 并委托记录结果。
    const test = (name, action) => record(`E03-CONTRACT-${String(++step).padStart(2, '0')}`, name, action);
    const env = { ...process.env, MYSQL_PWD: coordinator.password,
        CHAT_VARIFY_EMAIL_USER: 'fixture@example.invalid',
        CHAT_VARIFY_REDIS_PASSWORD: coordinator.password };
    delete env.CHAT_CONFIG;
    delete env.LD_PRELOAD;
    /** 通过所属监督器启动一个进程，登记退出观察与清理报告。 */
    function start(name, executable, args, extra = {}) {
        const report = path.join(root, `${name}-${children.length}.json`);
        const child = spawn(supervisor, ['supervise', executable, root, report, ...args], {
            env: { ...env, ...extra, LD_LIBRARY_PATH: `${path.dirname(supervisor)}:${path.dirname(executable)}` },
            windowsHide: true, stdio: ['ignore', 'ignore', 'ignore']
        });
        const owned = { name, child, report };
        owned.exited = new Promise(
            // 观察子进程退出或启动错误。
            resolve => { child.once('error',
            // 将子进程启动失败转换为负退出结果。
            () => resolve(-1)); child.once('exit', resolve); });
        children.push(owned);
        return owned;
    }
    /** 释放指定名称的端口保留并移除所有权记录。 */
    async function release(...names) {
        for (const name of names) if (leases[name]) { await leases[name].release(); delete leases[name]; }
    }
    /** 释放角色端口后写隔离 INI，并启动对应正式服务。 */
    async function native(role, configuredTopology = topology) {
        const binary = role.startsWith('Chat') ? 'ChatServer' : role;
        const keys = { GateServer: ['gate'], ChatA: ['chatA', 'rpcA'], ChatB: ['chatB', 'rpcB'] };
        await release(...keys[role]);
        const file = path.join(root, `${role}.ini`);
        fs.writeFileSync(file, nativeConfig(configuredTopology, role, { password: coordinator.password,
            redis: coordinator.config.ports.redis, mysql: coordinator.config.ports.mysql }, path.join(root, 'logs')), { mode: 0o600 });
        servers[role] = start(role, path.join(bundle, binary, binary), ['--config', file]);
        return servers[role];
    }
    /** 通过生成 gRPC 接口有界调用 Varify，结束后关闭客户端。 */
    async function rpc(service, method, request) {
        const definition = grpc.loadPackageDefinition(loader.loadSync(path.resolve(__dirname, '../../proto',
            'varify.proto')));
        const client = new definition.message[service](`127.0.0.1:${ports.varify}`,
            grpc.credentials.createInsecure());
        try {
            // 把有截止时间的 gRPC 调用转换为 Promise。
            return await new Promise((resolve, reject) => client[method](request, { deadline: Date.now() + 1500 },
                // 传播 gRPC 错误或交付响应。
                (error, result) => error ? reject(error) : resolve(result)));
        } finally { client.close(); }
    }
    /** 启动一个真实 Qt 测试客户端，等待本地控制通道就绪并返回观察到的 PID。 */
    async function client(name) {
        const control = new ClientControl(root, name);
        controls.push(control);
        await control.listen();
        const owned = start(name, clientBinary, ['--control', control.endpoint]);
        owned.control = control;
        const ready = await control.ready();
        return { control, owned, pid: ready.pid };
    }
    /** 等待实例连接数达到预期，同时只维护仍健康的控制通道。 */
    async function count(name, expected) {
        // 筛选尚未失败的客户端控制器。
        await waitForConnectionCount(coordinator, name, expected, controls.filter(control => !control.failed));
    }
    // 返回本次 Gate 的 loopback HTTP 基址。
    const gate = () => `http://127.0.0.1:${ports.gate}`;
    /** 通过真实验证码、注册和登录流程认证用户，可仅注册或复用既有账号。 */
    async function authenticate(instance, user, register = true, registerOnly = false) {
        if (register) {
            secrets.push(user.password);
            recipients.push(user.email);
            assert.equal((await instance.control.command('verify', { gate: gate(), email: user.email })).error, 0);
            let code;
            // 轮询本次收件人的 Mailpit 邮件并提取验证码。
            await poll(async () => {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${user.email}`)}`);
                if (!found.messages.length) return false;
                const mail = await coordinator.mailApi(`/api/v1/message/${found.messages[0].ID}`);
                // Same production email format used by the accepted four-process fixture.
                code = /验证码为([a-zA-Z0-9-]+)请/.exec(mail.Text)?.[1];
                return Boolean(code);
            }, 5000);
            secrets.push(code);
            if (selector !== '3D-00' && user.logical === 'alice') {
                // 验证错误验证码、重复注册及错误登录均拒绝，正确注册只创建一个用户。
                await record('E03-JOURNEY-06', 'public account failures do not create duplicate users or authenticate', async () => {
                    const registration = { gate: gate(), email: user.email, name: user.name,
                        password: user.password, code };
                    assert.notEqual((await instance.control.command('register', { ...registration, code: `${code}-wrong` })).error, 0);
                    await sql.execute(`USE \`${topology.database}\``);
                    assert.equal(await sql.execute('SELECT COUNT(*) FROM user'), '0');
                    assert.equal((await instance.control.command('register', registration)).error, 0);
                    assert.notEqual((await instance.control.command('register', registration)).error, 0);
                    assert.equal(await sql.execute('SELECT COUNT(*) FROM user'), '1');
                    const denied = await instance.control.command('login', { gate: gate(), email: user.email,
                        password: `${user.password}-wrong` });
                    assert.equal(denied.status, 'login-failed');
                    assert.equal((await instance.control.command('snapshot')).active, false);
                });
            } else {
                assert.equal((await instance.control.command('register', { gate: gate(), email: user.email,
                    name: user.name, password: user.password, code })).error, 0);
            }
        }
        if (registerOnly) return;
        const result = await instance.control.command('login', { gate: gate(), email: user.email, password: user.password });
        assert.equal(result.status, 'authenticated');
        user.uid = result.uid;
        return { logical: user.logical, pid: instance.pid, ...result };
    }
    let alice, bob;
    try {
        for (const name of ['gate', 'varify', 'chatA', 'rpcA', 'chatB', 'rpcB']) {
            leases[name] = await reserve(); ports[name] = leases[name].port;
        }
        topology = createTopology(coordinator.config.runId, ports);
        sql = new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD', coordinator.config.ids.mysql,
            'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET', '--user=root', ...mysqlArgs], env);
        // 创建本次隔离数据库并运行正式 schema 迁移。
        await test('fresh isolated application schema', async () => {
            await sql.execute(`CREATE DATABASE \`${topology.database}\``);
            await new SchemaMigration(sql, topology.database).apply();
        });
        // 写入隔离邮件配置并启动正式 Varify，等待验证码 RPC 就绪。
        await test('Gate, Varify and both Chat instances are ready', async () => {
            await release('varify');
            const file = path.join(root, 'varify.json');
            fs.writeFileSync(file, JSON.stringify({ email: { host: '127.0.0.1', port: coordinator.config.ports.smtp,
                secure: false, auth: 'none', deadlineMs: 2000 },
                redis: { host: '127.0.0.1', port: coordinator.config.ports.redis } }), { mode: 0o600 });
            start('VarifyServer', process.execPath, [path.resolve(__dirname, '../../VarifyServer/server.js'), '--config', file],
                { CHAT_VARIFY_BIND_ADDRESS: `127.0.0.1:${ports.varify}` });
            const email = `ready-${topology.runId}@example.invalid`; recipients.push(email);
            // 通过真实验证码 RPC 判断 Varify 已可服务。
            await poll(async () => Number((await rpc('VarifyService', 'GetVarifyCode', { email })).error) === 0, 30000);
            for (const role of ['ChatA', 'ChatB']) {
                await native(role);
                const port = role === 'ChatA' ? ports.chatA : ports.chatB;
                // 用原生驱动核对 Chat 对非法登录返回明确业务错误。
                await poll(async () => {
                    const value = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 10000,
                        env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor),
                            CHAT_FOUR_WIRE: JSON.stringify({ port, login: { uid: -1, token: 'invalid' } }) } }));
                    return Number.isInteger(value.error) && value.error !== 0;
                }, 30000);
            }
            await native('GateServer');
            // 用公开测试路由确认 Gate HTTP 就绪。
            await poll(async () => (await fetch(`${gate()}/get_test`, { signal: AbortSignal.timeout(1500) })).status === 200, 30000);
        });
        // 启动两套独立 Qt 客户端，核对 PID 不同且初始未登录。
        await test('two live production client processes', async () => {
            alice = await client('alice'); bob = await client('bob');
            assert.notEqual(alice.pid, bob.pid);
            for (const instance of [alice, bob]) assert.equal((await instance.control.command('snapshot')).active, false);
        });
        // 创建本次用户并认证到各自实例，按选择器准备额外权限测试用户。
        await test('public registration and real distinct Gate discovery', async () => {
            for (const user of topology.users) {
                const password = randomUUID().replaceAll('-', '').slice(0, 12);
                users.push({ ...user, password }); secrets.push(password);
            }
            const first = await authenticate(alice, users[0]);
            await count(topology.servers[0].name, 1);
            if (['3D-02', '3D-03-history', '3D-03', '3D'].includes(selector)) {
                const outsider = { logical: 'outsider', name: `outsider_${topology.runId}`,
                    email: `outsider-${topology.runId}@example.invalid`, password: randomUUID().slice(0, 12) };
                secrets.push(outsider.password);
                await authenticate(bob, outsider, true, true);
                await sql.execute(`USE \`${topology.database}\``);
                outsider.uid = Number(await sql.execute(`SELECT uid FROM user WHERE email='${outsider.email}'`));
                assert.ok(Number.isSafeInteger(outsider.uid) && outsider.uid > 0);
                auxiliaryUsers.push(outsider);
            }
            const second = await authenticate(bob, users[1]);
            await count(topology.servers[1].name, 1);
            assertConnectedClients(topology, [first, second]);
            topology.clients = [first, second];
        });
        if (selector !== '3D-00') {
            // 验证错误 Token 和跨用户 Token 均不能登录 Chat，原客户端状态不受影响。
            await record('E03-JOURNEY-07', 'Chat rejects mismatched and cross-account tokens without disturbing sessions', async () => {
                const redis = await coordinator.redis();
                let token;
                try { token = await redis.hget(String(users[0].uid), `utoken_${users[0].uid}`); }
                finally { redis.disconnect(); }
                assert.ok(token); secrets.push(token);
                for (const login of [{ uid: users[0].uid, token: `${token}-wrong` }, { uid: users[1].uid, token }]) {
                    const result = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 10000,
                        env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor),
                            CHAT_FOUR_WIRE: JSON.stringify({ port: ports.chatA, login }) } }));
                    assert.notEqual(result.error, 0);
                }
                assert.equal((await alice.control.command('snapshot')).uid, users[0].uid);
                assert.equal((await bob.control.command('snapshot')).uid, users[1].uid);
            });
        }
        let chatId;
        if (selector !== '3D-00') {
            await sql.execute(`USE \`${topology.database}\``);
            chatId = await runFriendshipCases({ alice, bob, users, sql, record });
        }
        const uuid = randomUUID();
        // 通过 Qt 创建会话和发送消息，核对双方快照及数据库内容。
        await test('production models correlate a durable cross-instance message', async () => {
            // 定位创建会话的控制响应，保留数值错误码。
            const created = await contractStep('client-create', async () => {
                const result = await alice.control.command('create', { toUid: users[1].uid });
                assert.equal(result.error, 0);
                return result;
            });
            chatId = created.chatId;
            // 核对服务返回了有效会话编号。
            await contractStep('client-chat-id', async () => { assert.ok(chatId > 0); });
            // 定位消息发送的控制响应，不记录正文。
            await contractStep('client-send', async () => {
                assert.equal((await alice.control.command('send', { toUid: users[1].uid, chatId, uuid, text: fixture.text })).error, 0);
            });
            const expectedHash = createHash('sha256').update(fixture.text).digest('hex');
            let first, second;
            // 等待双方各出现一条消息且发送方已获得持久化标识。
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
        if (['3D-02', '3D-03-history', '3D-03', '3D'].includes(selector)) {
            await runMessagingCases({ alice, bob, users, sql, record, chatId, outsider: auxiliaryUsers[0] });
        }
        if (realAcceptance) {
            leases.resource = await reserve();
            const resourcePort = leases.resource.port;
            await release('resource');
            const file = path.join(root, 'ResourceServer.ini');
            fs.writeFileSync(file, `[ResourceServer]\nHost=127.0.0.1\nPort=${resourcePort}\nStorageRoot=${path.join(root, 'resources')}\n` +
                `[Redis]\nHost=127.0.0.1\nPort=${coordinator.config.ports.redis}\nPassword=` + coordinator.password + `\n` +
                `[Mysql]\nHost=127.0.0.1\nPort=${coordinator.config.ports.mysql}\nUser=root\nPassword=` +
                coordinator.password + `\nSchema=${topology.database}\n` +
                `[Log]\nLogDir=${path.join(root, 'logs')}\nLevel=warn\n`, { mode: 0o600 });
            start('ResourceServer', path.join(bundle, 'ResourceServer', 'ResourceServer'), ['--config', file]);
            const resourceUrl = `http://127.0.0.1:${resourcePort}`;
            env.CHAT_RESOURCE_URL = resourceUrl;
            // 资源健康路由只确认启动，后续专项以真实上传与下载断言业务可用。
            await poll(async () =>
                (await fetch(`${resourceUrl}/health`, { signal: AbortSignal.timeout(1500) })).status === 200, 30000);
            topology.releaseClients = await runFirstReleaseCases({ record, client, authenticate, users: auxiliaryUsers, root, resourceUrl, sql,
                // 经公开 Gate 登录取得本次测试账号身份，用既有原生驱动验证群代次拒绝并恢复 Qt 登录。
                probeGroupSend: async (instance, user, chat, epoch) => {
                    assert.equal((await instance.control.command('logout')).status, 'logged-out');
                    try {
                        // Match the existing Qt registration/login encoding for these generated ASCII passwords.
                        const password =
                            // 按现有长度异或协议编码测试密码。
                            [...user.password].map(value =>
                            String.fromCharCode(value.charCodeAt(0) ^ (user.password.length % 256))).join('');
                        const login = await (await fetch(`${gate()}/user_login`, { method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({ email:
                                user.email, password }), signal: AbortSignal.timeout(5000) })).json();
                        assert.equal(login.error, 0); assert.equal(login.uid, user.uid);
                        assert.ok(login.token); secrets.push(password, login.token);
                        const probeUuid = randomUUID();
                        const result = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 15000,
                            env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor), CHAT_FOUR_WIRE: JSON.stringify({
                                port: Number(login.port), login: { uid: user.uid, token:
                                    login.token,
                                    capabilities: ['group_membership_v1'] },
                                requests: [{ id: 1016, body: { from_uid: user.uid, to_uid: 0, chat_id: chat,
                                    chat_type: 'group', membership_epoch: epoch,
                                    text_array: [{ msg_uuid: probeUuid, msg_content: 'rejected membership probe' }] } }] }) } }));
                        assert.equal(result.error, 0);
                        assert.equal(result.responses.length, 1);
                        const response = result.responses[0];
                        assert.ok(Number.isInteger(response.error) && response.error > 0);
                        assert.equal(response.commit_error, 'InvalidMembership');
                        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${probeUuid}'`), '0');
                        return response;
                    } finally { await authenticate(instance, user, false); }
                },
                // 在各独立业务步骤之间串行保活，避免并发打断客户端正在执行的控制命令。
                keepAlive: async () => {
                    for (const control of controls) if (!control.failed) await control.command('snapshot');
                },
                // 退出新增客户端并释放控制通道，恢复原双账号连接数断言的前置状态。
                retire: async instance => {
                    let acknowledged = false;
                    if (!instance.control.failed) {
                        assert.equal((await instance.control.command('stop')).status, 'stopped');
                        acknowledged = true;
                    }
                    await stop(instance.owned, 0, { graceful: acknowledged });
                    await instance.control.close();
                } });
            await count(topology.servers[0].name, 1);
            await count(topology.servers[1].name, 1);
        }
        // 停止 Alice 客户端并等待实例计数归零，确认 Bob 仍在线且消息保留。
        await test('one client exits while the peer retains its independent session', async () => {
            assert.equal((await alice.control.command('stop')).status, 'stopped');
            await stop(alice.owned, 0, { graceful: true });
            await alice.control.close();
            await count(topology.servers[0].name, 0);
            const remaining = await bob.control.command('snapshot');
            assert.equal(remaining.active, true); assert.equal(remaining.uid, users[1].uid);
            assert.equal((await bob.control.command('snapshot', { chatId })).messages.length, ['3D-02', '3D-03-history', '3D-03', '3D'].includes(selector) ? 5 : 1);
        });
        if (['3D-02', '3D-03-history', '3D-03', '3D'].includes(selector)) {
            await runOfflineMessageCase({ bob, users, sql, record, chatId });
            // 验证已认证连接不能伪造另一发送方提交消息。
            await record('E03-XMSG-08', 'authenticated wire rejects forged sender identity before persistence', async () => {
                const redis = await coordinator.redis();
                let token;
                try { token = await redis.hget(String(users[0].uid), `utoken_${users[0].uid}`); }
                finally { redis.disconnect(); }
                assert.ok(token); secrets.push(token);
                const spoofUuid = randomUUID();
                const result = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 15000,
                    env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor), CHAT_FOUR_WIRE: JSON.stringify({
                        port: ports.chatA, login: { uid: users[0].uid, token }, requests: [{ id: 1016, expect_disconnect: true,
                            body: { from_uid: users[1].uid, to_uid: users[0].uid, chat_id: chatId,
                                text_array: [{ msg_uuid: spoofUuid, msg_content: 'forged sender' }] } }] }) } }));
                assert.equal(result.error, 0);
                assert.equal(result.responses.length, 1);
                assert.equal(result.responses[0].disconnected, true);
                assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${spoofUuid}'`), '0');
            });
        }
        if (['3D-03-history', '3D-03', '3D'].includes(selector)) {
            // 重启 Alice 客户端并用原账号恢复，核对新 PID 与原实例身份。
            const recoveredAlice = await runHistoryRecoveryCases({ bob, users, sql, record, chatId, restartClient: async () => {
                await count(topology.servers[0].name, 0);
                const recovered = await client('alicerecovered');
                const expectedUid = users[0].uid;
                const observed = await authenticate(recovered, users[0], false);
                assert.notEqual(recovered.pid, alice.pid);
                assert.equal(observed.uid, expectedUid);
                assert.equal(observed.active, true);
                assert.equal(observed.host, topology.host);
                assert.equal(observed.port, topology.servers[0].port);
                topology.recoveredClients = [{ logical: users[0].logical, previousPid: alice.pid,
                    pid: recovered.pid, uid: observed.uid, active: observed.active, host: observed.host, port: observed.port }];
                return recovered;
            } });
            if (['3D-03', '3D'].includes(selector)) {
                // 读取所属监督器记录的进程身份。
                const identity = owned => JSON.parse(fs.readFileSync(`${owned.report}.identity`));
                const ready =
                    // 轮询指定 Chat 端点直到非法登录得到稳定业务拒绝。
                    async port => poll(
                    // 调用原生协议驱动确认目标 Chat 已就绪。
                    async () => {
                    const result = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 10000,
                        env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor),
                            CHAT_FOUR_WIRE: JSON.stringify({ port, login: { uid: -1, token: 'invalid' } }) } }));
                    return result.error > 0;
                }, 30000);
                // 停止并重启指定 Chat，等待客户端失活和新进程就绪，记录代际身份。
                const restart = async (role, clientInstance, configuredTopology = topology) => {
                    const previous = servers[role];
                    const oldIdentity = identity(previous);
                    await stop(previous);
                    // 等待被重启实例上的客户端观察到连接失活。
                    await poll(async () => !(await clientInstance.control.command('snapshot')).active, 10000);
                    await native(role, configuredTopology);
                    const index = role === 'ChatA' ? 0 : 1;
                    await ready(configuredTopology.servers[index].port);
                    const nextIdentity = identity(servers[role]);
                    assert.ok(oldIdentity.pid !== nextIdentity.pid || oldIdentity.creationTime !== nextIdentity.creationTime);
                    topology.serverRestarts ||= [];
                    topology.serverRestarts.push({ logical: role, previous: oldIdentity, current: nextIdentity,
                        advertisedPort: topology.servers[index].port, listenPort: configuredTopology.servers[index].port });
                };
                await runFaultRecoveryCases({ alice: recoveredAlice, bob, users, sql, record, chatId,
                    // 将 Chat A TCP 后端移到新端口并插入故障中继，保留 RPC 和选服身份。
                    prepareRelay: async (dropUuid, replayUuid) => {
                        const lease = await reserve();
                        leases.faultBackend = lease;
                        const backendPort = lease.port;
                        // Preserve RPC identity and discovery; only this fault path inserts the relay.
                        // 只覆盖 Chat A 的 TCP 端口，其余实例字段保持不变。
                        const configured = { ...topology, servers: topology.servers.map((server, index) =>
                            index === 0 ? { ...server, port: backendPort } : server) };
                        await release('faultBackend');
                        await restart('ChatA', recoveredAlice, configured);
                        const file = path.join(evidenceRoot, 'fault-relay.json');
                        const owned = start('FrameFaultRelay', supervisor, ['relay', file], {
                            CHAT_RELAY_CONFIG: JSON.stringify({ port: ports.chatA, backend: backendPort, dropUuid, replayUuid }) });
                        // 读取当前故障中继的 JSON 证据。
                        const read = () => JSON.parse(fs.readFileSync(file));
                        // 等待故障中继报告已就绪。
                        await poll(async () => read().ready === true, 10000);
                        return { owned, read };
                    },
                    // 重新认证指定客户端，核对选服实例和连接计数恢复。
                    relogin: async (instance, index) => {
                        await count(topology.servers[index].name, 0);
                        await count(topology.servers[1 - index].name, 1);
                        const uid = users[index].uid;
                        const observed = await authenticate(instance, users[index], false);
                        assert.equal(observed.uid, uid);
                        assert.equal(observed.port, topology.servers[index].port);
                        assert.equal(observed.active, true);
                        await count(topology.servers[index].name, 1);
                    },
                    // 重启 Chat B 并观察 Bob 连接失活。
                    restartBob: () => restart('ChatB', bob),
                    // 停止恢复客户端后以原生驱动验证非法历史游标被拒绝。
                    invalidHistory: async () => {
                        assert.equal((await recoveredAlice.control.command('stop')).status, 'stopped');
                        await stop(recoveredAlice.owned, 0, { graceful: true });
                        await recoveredAlice.control.close();
                        const redis = await coordinator.redis();
                        let token;
                        try { token = await redis.hget(String(users[0].uid), `utoken_${users[0].uid}`); }
                        finally { redis.disconnect(); }
                        assert.ok(token); secrets.push(token);
                        const result = JSON.parse(await runCommand(supervisor, ['chat'], { timeout: 15000,
                            env: { ...env, LD_LIBRARY_PATH: path.dirname(supervisor), CHAT_FOUR_WIRE: JSON.stringify({
                                port: ports.chatA, login: { uid: users[0].uid, token },
                                // 为每个非法游标构造历史请求帧。
                                requests: [-1, 'bad', 1.5, 2147483648].map(cursor => ({ id: 1027,
                                    body: { chat_id: chatId, current_msg_id: cursor } })) }) } }));
                        assert.equal(result.error, 0);
                        assert.equal(result.responses.length, 4);
                        for (const response of result.responses) {
                            assert.equal(response.error, 1001);
                            assert.ok(!response.msgs);
                        }
                    }, stopRelay: stop });
            }

        }
    } catch (error) { primary = error; }
    finally {
        const failures = [];
        const diagnostics = [];
        for (const control of [...controls].reverse()) {
            try {
                if (!control.failed) {
                    assert.equal((await control.command('stop')).status, 'stopped');
                    control.stopAcknowledged = true;
                }
            }
            catch { failures.push('client-stop'); }
            try { await control.close(); } catch { failures.push('control-close'); }
        }
        for (const owned of [...children].reverse()) {
            try { await stop(owned, 0, { graceful: owned.control?.stopAcknowledged === true }); }
            catch (error) {
                failures.push(`stop-${owned.name}`);
                const diagnostic = caseDiagnostic(error);
                if (diagnostic) diagnostics.push(diagnostic);
            }
        }
        for (const lease of Object.values(leases)) {
            try { await lease.release(); } catch { failures.push('lease-close'); }
        }
        try {
            const redis = await coordinator.redis();
            try {
                // 把本次收件人映射为需清理的验证码键。
                const keys = recipients.map(email => `code_${email}`);
                // 提取本次用户编号供所属数据清理。
                for (const user of [...users, ...auxiliaryUsers].filter(user => user.uid)) {
                    for (const prefix of ['', 'auth_version_', 'utoken_', 'uip_', 'ubaseinfo_', 'usessionid_', 'usessiontoken_', 'lock_']) keys.push(`${prefix}${user.uid}`);
                }
                if (keys.length) { await redis.del(...keys); assert.equal(await redis.exists(...keys), 0); }
                if (topology) for (const server of topology.servers) assert.equal(await redis.hexists('logincount', server.name), 0);
            } finally { redis.disconnect(); }
            for (const email of recipients) {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`);
                if (found.messages.length) await coordinator.mailApi('/api/v1/messages', { method: 'DELETE',
                    // 提取本次邮件标识供定向删除。
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
        // 确认编码后的证据不含本次生成的敏感值。
        const redacted = secrets.every(secret => !encoded.includes(secret));
        fs.writeFileSync(path.join(evidenceRoot, 'topology.json'), JSON.stringify(evidence.topology, null, 2));
        fs.writeFileSync(path.join(evidenceRoot, 'application-teardown.json'), JSON.stringify({
            complete: evidence.complete, failures, diagnostics, primaryDiagnostic: primary && caseDiagnostic(primary) }));
        fs.writeFileSync(path.join(evidenceRoot, 'redaction.json'), JSON.stringify({ complete: redacted }));
        try {
            // 断言所有清理步骤成功，且最终证据已脱敏。
            await record('E03-CONTRACT-12', 'reverse client service data and control cleanup', async () => {
                assert.deepEqual(failures, []); assert.equal(redacted, true);
            });
        } catch (error) { primary ||= error; }
    }
    if (primary) throw primary;
}

module.exports = { runFiveProcessCases };
