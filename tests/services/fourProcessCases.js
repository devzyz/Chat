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

/** 保留一个临时 loopback 端口，并返回带释放操作的所有权对象。 */
async function reserve() {
    const server = net.createServer();
    await new Promise(/** 等待端口绑定成功，监听失败则传播错误。 */ (resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    return { port: server.address().port, release: /** 释放本对象保留的监听端口并返回完成等待。 */ () => new Promise(/** 把端口释放回调转换为 Promise。 */ resolve => server.close(resolve)) };
}

/** 有界停止所属进程，核对监督报告与预期退出码，成功后标记已停止。 */
async function stop(owned, expected = 0) {
    if (owned.stopped) return;
    const fail = /** 用稳定的服务名和分类抛出停止失败。 */ category => { throw new Error(`FourProcess:${owned.name}:${category}`); };
    owned.child.kill('SIGTERM');
    let timer;
    const result = await Promise.race([owned.exited, new Promise(/** 建立十五秒停止截止等待。 */ resolve => { timer = setTimeout(/** 停止期限到达后返回超时标记。 */ () => resolve('timeout'), 15000); })]);
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

/** 在隔离依赖和临时目录中运行四个正式服务，验证协议、故障恢复与清理。 */
async function runFourProcessCases(coordinator, record) {
    const { poll, runCommand, contractStep } = require('./dependencyCoordinator');
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
    const test = /** 分配连续四进程 Test ID 并委托记录执行结果。 */ (name, action) => record(`T10-4PROC-${String(++counter).padStart(2, '0')}`, name, action);
    /** 通过所属原生监督器启动进程，保存报告路径与退出观察。 */
    async function start(name, executable, args, env = environment) {
        const report = path.join(root, `${name}-${children.length}.json`);
        const child = spawn(driver, ['supervise', executable, root, report, ...args], {
            env: { ...env, LD_LIBRARY_PATH: `${path.dirname(driver)}:${path.dirname(executable)}` }, windowsHide: true,
            stdio: ['ignore', 'ignore', 'ignore']
        });
        const owned = { child, report, name };
        owned.exited = new Promise(/** 观察监督进程退出或启动失败。 */ resolve => { child.once('error', /** 把监督器启动错误转换为失败结果。 */ () => resolve(-1)); child.once('exit', resolve); });
        children.push(owned);
        return owned;
    }
    /** 生成对应正式服务的隔离配置，指向本次依赖和数据库。 */
    function writeConfig(name) {
        const common = `[Redis]\nHost=127.0.0.1\nPort=${coordinator.config.ports.redis}\nPassword=${coordinator.password}\n` +
            (name === 'StatusServer' ? '' : `[Mysql]\nHost=127.0.0.1\nPort=${coordinator.config.ports.mysql}\nUser=root\nPassword=${coordinator.password}\nSchema=${database}\n`
            ) +
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
    /** 释放服务保留端口后启动正式原生产物。 */
    async function native(name) {
        if (leases[name]) { await leases[name].release(); delete leases[name]; }
        if (name === 'ChatServer' && leases.ChatRPC) { await leases.ChatRPC.release(); delete leases.ChatRPC; }
        return start(name, path.join(bundle, name, name), ['--config', writeConfig(name)]);
    }
    /** 写入本次邮件及依赖配置并启动正式验证码服务。 */
    async function varify() {
        if (leases.VarifyServer) { await leases.VarifyServer.release(); delete leases.VarifyServer; }
        const config = path.join(root, 'varify.json');
        fs.writeFileSync(config, JSON.stringify({ email: { host: '127.0.0.1', port: coordinator.config.ports.smtp,
            secure: false, auth: 'none', deadlineMs: 2000 }, mysql: { host: '127.0.0.1', port: coordinator.config.ports.mysql },
            redis: { host: '127.0.0.1', port: coordinator.config.ports.redis } }));
        return start('VarifyServer', process.execPath, [path.resolve(__dirname, '../../VarifyServer/server.js'), '--config', config],
            { ...environment, CHAT_VARIFY_BIND_ADDRESS: `127.0.0.1:${ports.VarifyServer}` });
    }
    /** 向本次 Gate 发起有界 HTTP 请求，要求成功状态后解析对应响应。 */
    async function http(route, body) {
        const response = await fetch(`http://127.0.0.1:${ports.GateServer}${route}`, {
            method: body ? 'POST' : 'GET', body: body ? JSON.stringify(body) : undefined,
            headers: { 'Content-Type': 'application/json' }, signal: AbortSignal.timeout(10000)
        });
        assert.equal(response.status, 200);
        return body ? response.json() : response.text();
    }
    /** 通过原生驱动执行 Chat 登录及请求序列，返回解析后的结果。 */
    async function wire(login, requests = []) {
        return JSON.parse(await runCommand(driver, ['chat'], {
            env: { ...environment, LD_LIBRARY_PATH: path.dirname(driver),
                CHAT_FOUR_WIRE: JSON.stringify({ port: ports.ChatServer, login, requests }) }, timeout: 20000
        }));
    }
    /** 对 Status 或 Varify 执行有界 gRPC 就绪请求，始终关闭客户端。 */
    async function rpcReady(service, method, request) {
        const file = service === 'StatusService' ? 'status.proto' : 'varify.proto';
        const definition = grpc.loadPackageDefinition(loader.loadSync(path.resolve(__dirname, '../../proto', file)));
        const client = new definition.message[service](`127.0.0.1:${ports[service === 'StatusService' ? 'StatusServer' : 'VarifyServer']}`, grpc.credentials.createInsecure());
        try {
            return await new Promise(/** 将有截止时间的 gRPC 调用结果接入 Promise。 */ (resolve, reject) => client[method](request, { deadline: Date.now() + 2000 }, /** 传播 RPC 错误或返回响应值。 */ (error, value) => error ? reject(error) : resolve(value)));
        } finally { client.close(); }
    }
    /** 通过真实邮件验证码注册并登录本次唯一用户，保存后续协议身份。 */
    async function register(index) {
        const email = `${coordinator.config.runId}-four-${index}@example.invalid`;
        recipients.push(email);
        await contractStep('gate-verify', /** 定位验证码请求失败，不记录邮箱或响应正文。 */ async () => {
            assert.equal((await http('/get_varifycode', { email })).error, 0);
        });
        let code;
        await poll(/** 轮询所属测试收件人的邮件并提取验证码。 */ async () => {
            const result = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`);
            if (!result.messages.length) return false;
            const mail = await coordinator.mailApi(`/api/v1/message/${result.messages[0].ID}`);
            code = /验证码为([a-zA-Z0-9-]+)请/.exec(mail.Text)?.[1];
            return Boolean(code);
        }, 5000);
        const password = randomUUID();
        await contractStep('gate-register', /** 定位注册请求失败，仅保留数值错误码。 */ async () => {
            assert.equal((await http('/user_register', { user: `four_${index}`, email, passwd: password,
                confirm: password, varifycode: code })).error, 0);
        });
        const login = await contractStep('gate-login', /** 定位登录请求失败，Token 仅留在私有运行内存。 */ async () => {
            const result = await http('/user_login', { email, password });
            assert.equal(result.error, 0);
            return result;
        });
        await contractStep('gate-selection', /** 核对选服端口，不输出实际端点。 */ async () => {
            assert.equal(Number(login.port), ports.ChatServer);
        });
        users.push({ uid: login.uid, token: login.token, email, password });
    }
    try {
        for (const name of ['VarifyServer', 'StatusServer', 'ChatServer', 'ChatRPC', 'GateServer']) {
            leases[name] = await reserve(); ports[name] = leases[name].port;
        }
        await test('fresh application schema', /** 创建本次四进程数据库并执行正式迁移。 */ async () => {
            await session.execute(`CREATE DATABASE \`${database}\``);
            await new SchemaMigration(session, database).apply();
        });
        await test('Varify production gRPC ready', /** 启动 Varify 并以真实验证码请求等待协议就绪。 */ async () => {
            await varify();
            const email = `${coordinator.config.runId}-four-ready@example.invalid`; recipients.push(email);
            await poll(/** 检查验证码请求返回成功。 */ async () => (await rpcReady('VarifyService', 'GetVarifyCode', { email })).error === 0, 30000);
        });
        await test('Status production gRPC ready', /** 启动 Status 并等待非法登录返回业务拒绝。 */ async () => {
            await native('StatusServer');
            await poll(/** 检查 Status 已能处理并拒绝非法登录。 */ async () => Number((await rpcReady('StatusService', 'Login', { uid: -1, token: 'invalid' })).error) !== 0, 30000);
        });
        await test('Chat production TCP ready', /** 启动 Chat 并等待原生协议登录拒绝响应。 */ async () => {
            await native('ChatServer');
            await poll(/** 检查 Chat 已能处理并拒绝非法登录。 */ async () => (await wire({ uid: -1, token: 'invalid' })).error !== 0, 30000);
        });
        await test('Gate production HTTP ready', /** 启动 Gate 并等待公开测试路由响应。 */ async () => { await native('GateServer'); await poll(/** 检查 Gate 测试路由返回非空响应。 */ async () => Boolean(await http('/get_test')), 30000); });
        await test('mail registration login and server selection', /** 通过真实入口注册两个测试用户。 */ async () => { await register(1); await register(2); });
        let chat;
        await test('TCP authentication and public private-chat creation', /** 通过 Chat 协议创建双方私聊并核对正的会话标识。 */ async () => {
            const result = await wire(users[0], [{ id: 1023, body: { self_id: users[0].uid, other_id: users[1].uid } }]);
            assert.equal(result.error, 0); assert.equal(result.responses[0].error, 0);
            chat = result.responses[0].chat_id; assert.ok(chat > 0);
        });
        const uuid = randomUUID();
        const message = /** 构造带固定 UUID 的测试消息请求，供首次提交与重试共用。 */ () => ({ id: 1016, body: { from_uid: users[0].uid, to_uid: users[1].uid, chat_id: chat,
            text_array: [{ msg_uuid: uuid, msg_content: 'four-process synthetic message' }] } });
        await test('public message commit and durable UUID', /** 发送消息并确认隔离数据库只持久化一条记录。 */ async () => {
            const result = await wire(users[0], [message()]);
            assert.equal(result.responses[0].error, 0);
            // MysqlSession uses a private delimiter and accepts one statement;
            // the mysql client's USE command must not consume a following query.
            await session.execute(`USE \`${database}\``);
            assert.equal(await session.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
        });
        await test('disconnect retry preserves single durable message', /** 重复提交同一消息，核对持久化标识稳定且没有重复行。 */ async () => {
            const before = await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`);
            const result = await wire(users[0], [message()]); assert.equal(result.responses[0].error, 0);
            assert.equal(await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`), before);
            assert.equal(await session.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
        });
        await test('occupied Gate port fails without disturbing original', /** 验证重复 Gate 启动因端口冲突退出，原服务仍可用。 */ async () => {
            const conflict = await native('GateServer');
            await poll(/** 等待冲突的 Gate 进程退出。 */ async () => conflict.child.exitCode !== null, 15000);
            await stop(conflict, 1); assert.ok(await http('/get_test'));
        });
        await test('SMTP unavailable returns business failure within deadline', /** 停止 Mailpit 验证验证码失败，再恢复所属邮件服务。 */ async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            const email = `${coordinator.config.runId}-four-unavailable@example.invalid`; recipients.push(email);
            try { assert.notEqual((await http('/get_varifycode', { email })).error, 0); }
            finally { await coordinator.lifecycle('mailpit', 'start'); }
            await poll(/** 探测 Mailpit 就绪接口以确认恢复。 */ () => coordinator.mailApi('/readyz'), 5000);
        });
        await test('SMTP recovery uses refreshed mapping', /** 重启正式 Varify 后验证 Gate 验证码链路恢复。 */ async () => {
            await stop(children.find(/** 定位当前仍运行的 Varify 所属进程。 */ value => value.name === 'VarifyServer' && !value.stopped));
            await varify();
            const email = `${coordinator.config.runId}-four-recovered@example.invalid`; recipients.push(email);
            await poll(/** 等待重启后的验证码链路返回成功。 */ async () => (await http('/get_varifycode', { email })).error === 0, 30000);
        });
        await test('Redis outage fails closed', /** 停止 Redis 验证登录失败，最终恢复 Redis 及本次认证设置。 */ async () => {
            await coordinator.lifecycle('redis', 'stop');
            try { assert.notEqual((await http('/user_login', { email: users[0].email, password: users[0].password })).error, 0); }
            finally {
                await coordinator.lifecycle('redis', 'start');
                await poll(/** 恢复 Redis 认证设置，结束后断开临时连接。 */ async () => {
                    const client = await coordinator.redis(null);
                    try { await client.config('SET', 'requirepass', coordinator.password); return true; }
                    finally { client.disconnect(); }
                }, 5000);
            }
        });
        await test('application recovery after dependency port remap', /** 逆序停止并重启全部服务，核对原用户能重新登录 Chat。 */ async () => {
            for (const owned of [...children].reverse()) await stop(owned);
            await varify(); await native('StatusServer'); await native('ChatServer'); await native('GateServer');
            await poll(/** 通过 Gate 重新取得 Token 后验证 Chat 登录成功。 */ async () => {
                const login = await http('/user_login', { email: users[0].email, password: users[0].password });
                if (login.error !== 0) return false;
                users[0].token = login.token;
                return (await wire(users[0])).error === 0;
            }, 30000);
        });
        await test('recovered retry retains original durable ID', /** 验证全服务重启后 UUID 重试仍复用原持久化消息。 */ async () => {
            const before = await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`);
            assert.equal((await wire(users[0], [message()])).responses[0].error, 0);
            assert.equal(await session.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`), before);
        });
        await test('Redis stores current instance routing', /** 等待所有测试连接关闭后，实例发布的登录计数归零。 */ async () => {
            const client = await coordinator.redis();
            try { await poll(/** 检查指定实例的 Redis 登录计数已为零。 */ async () => await client.hget('logincount', runName) === '0', 5000); }
            finally { client.disconnect(); }
        });
        await test('Gate shutdown interrupts a real blocked MySQL read', /** 持有数据库写锁制造在途登录，验证 Gate 停止有界完成并最终解锁。 */ async () => {
            await session.execute('LOCK TABLES user WRITE');
            const pending = http('/user_login', { email: users[0].email, password: users[0].password }).catch(/** 把预期的在途请求失败归一为空结果，避免未处理拒绝。 */ () => null);
            try {
                await poll(/** 观察隔离数据库中确有请求等待锁。 */ async () => Number(await coordinator.sql(
                    `SELECT COUNT(*) FROM information_schema.processlist WHERE DB='${database}' AND STATE LIKE '%lock%'`)) > 0, 1500);
                await stop(children.find(/** 定位本次仍运行的 Gate 进程。 */ value => value.name === 'GateServer' && !value.stopped));
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
                const keys = recipients.map(/** 映射本次收件人的验证码键以定向清理。 */ email => `code_${email}`);
                for (const user of users) for (const prefix of ['utoken_', 'uip_', 'ubaseinfo_', 'usessionid_', 'lock_']) keys.push(`${prefix}${user.uid}`);
                if (keys.length) { await client.del(...keys); assert.equal(await client.exists(...keys), 0); }
                assert.equal(await client.hexists('logincount', runName), 0);
            } finally { client.disconnect(); }
            for (const email of recipients) {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`);
                if (found.messages.length) await coordinator.mailApi('/api/v1/messages', { method: 'DELETE',
                    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ IDs: found.messages.map(/** 提取本次 Mailpit 邮件标识供删除。 */ value => value.ID) }) });
                assert.equal((await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${email}`)}`)).messages.length, 0);
            }
            await session.execute(`DROP DATABASE IF EXISTS \`${database}\``);
            assert.equal(await session.execute(`SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${database}'`), '0');
            for (const port of Object.values(ports)) {
                const listener = net.createServer();
                await new Promise(/** 重新绑定所属端口以验证服务已释放监听资源。 */ (resolve, reject) => { listener.once('error', reject); listener.listen(port, '127.0.0.1', resolve); });
                await new Promise(/** 释放用于端口清理验证的监听器。 */ resolve => listener.close(resolve));
            }
        } catch { failures.push('owned-data-cleanup'); }
        await session.close();
        if (failures.length === 0) fs.rmSync(root, { recursive: true });
        try { await record('T10-4PROC-18', 'reverse graceful process and data cleanup', /** 断言进程、端口和依赖数据清理均没有失败。 */ async () => { assert.deepEqual(failures, []); }); }
        catch (error) { primary ||= error; }
    }
    if (primary) throw primary;
}

module.exports = { reserve, stop, runFourProcessCases };
