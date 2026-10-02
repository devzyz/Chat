'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { randomUUID } = require('node:crypto');
const { setTimeout: delay } = require('node:timers/promises');
const { DependencyCoordinator, loadConfiguration, poll } = require('../dependencyCoordinator');
const { reserve, stop } = require('../fourProcessCases');
const { createTopology, nativeConfig } = require('../twoServerTopology');
const { SchemaMigration } = require('../../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
const { Client, post } = require('./client');
const { waitForConnectionCount } = require('../connectionCount');

/** 管理本次正式服务、临时数据、客户端及监督器，复用既有隔离实现。 */
class Environment {
    /** 初始化唯一运行身份，所有文件均位于独立临时目录。 */
    constructor(signal) {
        this.signal = signal;
        this.root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-performance-'));
        this.children = []; this.clients = []; this.leases = []; this.accounts = [];
        this.config = loadConfiguration({ ...process.env, CHAT_SERVICE_RUN_ID: randomUUID().replaceAll('-', ''),
            CHAT_SERVICE_HOST: '127.0.0.1' });
        this.coordinator = new DependencyCoordinator(this.config);
        this.bundle = path.resolve(process.env.CHAT_FOUR_BUNDLE);
        this.driver = path.join(this.bundle, 'driver', 'FourProcessDriver');
    }
    /** 用现有监督器启动所属服务，终止时检查监督证据。 */
    start(name, executable, args, extra = {}) {
        this.signal?.throwIfAborted();
        const report = path.join(this.root, `${name}.json`);
        const child = spawn(this.driver, ['supervise', executable, this.root, report, ...args], {
            windowsHide: true, stdio: ['ignore', 'ignore', 'ignore'], env: { ...process.env,
                ...extra, LD_LIBRARY_PATH: `${path.dirname(this.driver)}:${path.dirname(executable)}`, LD_PRELOAD: '' }
        });
        const owned = { name, child, report };
        owned.exited = new Promise(/** 保存监督器最终退出结果。 */ resolve => {
            child.once('error', /** 把启动错误保存为失败退出。 */ () => resolve(-1)); child.once('exit', resolve);
        });
        this.children.push(owned);
    }
    /** 分配临时端口并保存其释放所有权。 */
    async port() { const lease = await reserve(); this.leases.push(lease); return lease; }
    /** 初始化真实依赖、迁移及六个生产进程，并通过公开端点确认就绪。 */
    async setup() {
        for (const service of ['redis', 'mysql', 'mailpit']) await this.coordinator.inspect(service);
        await this.coordinator.bootstrap();
        const ports = {}, leases = {};
        for (const name of ['gate', 'status', 'varify', 'chatA', 'rpcA', 'chatB', 'rpcB']) {
            leases[name] = await this.port(); ports[name] = leases[name].port;
        }
        this.topology = createTopology(this.config.runId, ports);
        this.topology.database = this.config.database;
        const session = new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD', this.config.ids.mysql,
            'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET', '--user=root', ...mysqlArgs],
        { ...process.env, MYSQL_PWD: this.coordinator.password });
        let queue = Promise.resolve();
        this.sql = {
            /** 串行化观测查询，避免并行场景争用单个管理连接。 */
            execute(statement) {
                const operation = queue.then(/** 前一查询完成后提交当前 SQL。 */ () => session.execute(statement));
                queue = operation.catch(/** 本次错误由调用方处理，队列仍允许清理。 */ () => {});
                return operation;
            },
            /** 等待已提交查询后关闭真实管理连接。 */
            async close() { await queue; await session.close(); }
        };
        await this.sql.execute(`CREATE DATABASE \`${this.config.database}\``);
        this.coordinator.databaseCreated = true;
        await new SchemaMigration(this.sql, this.config.database).apply();
        await this.sql.execute(`USE \`${this.config.database}\``);
        const varify = path.join(this.root, 'varify.json');
        fs.writeFileSync(varify, JSON.stringify({ email: { host: '127.0.0.1', port: this.config.ports.smtp,
            secure: false, auth: 'none', deadlineMs: 10000 }, mysql: { host: '127.0.0.1', port: this.config.ports.mysql },
        redis: { host: '127.0.0.1', port: this.config.ports.redis } }), { mode: 0o600 });
        await leases.varify.release(); leases.varify.released = true;
        this.start('VarifyServer', process.execPath, [path.resolve(__dirname, '../../../VarifyServer/server.js'), '--config', varify],
            { CHAT_VARIFY_BIND_ADDRESS: `127.0.0.1:${ports.varify}`, CHAT_VARIFY_EMAIL_USER: 'fixture@example.invalid',
                CHAT_VARIFY_MYSQL_PASSWORD: this.coordinator.password, CHAT_VARIFY_REDIS_PASSWORD: this.coordinator.password });
        for (const role of ['StatusServer', 'ChatA', 'ChatB', 'GateServer']) {
            const keys = { StatusServer: ['status'], ChatA: ['chatA', 'rpcA'], ChatB: ['chatB', 'rpcB'], GateServer: ['gate'] }[role];
            for (const key of keys) { await leases[key].release(); leases[key].released = true; }
            const file = path.join(this.root, `${role}.ini`);
            fs.writeFileSync(file, nativeConfig(this.topology, role, { password: this.coordinator.password,
                mysql: this.config.ports.mysql, redis: this.config.ports.redis }, path.join(this.root, 'logs')), { mode: 0o600 });
            const binary = role.startsWith('Chat') ? 'ChatServer' : role;
            this.start(role, path.join(this.bundle, binary, binary), ['--config', file]);
        }
        const resource = await this.port();
        await resource.release(); resource.released = true;
        const file = path.join(this.root, 'ResourceServer.ini');
        fs.writeFileSync(file, `[ResourceServer]\nHost=127.0.0.1\nPort=${resource.port}\nStorageRoot=${this.root}/resources\n` +
            `[StatusServer]\nHost=127.0.0.1\nPort=${ports.status}\n[Mysql]\nHost=127.0.0.1\n` +
            `Port=${this.config.ports.mysql}\nUser=root\nPassword=${this.coordinator.password}\nSchema=${this.config.database}\n` +
            `[Log]\nLogDir=${this.root}/logs\nLevel=warn\n`, { mode: 0o600 });
        this.start('ResourceServer', path.join(this.bundle, 'ResourceServer', 'ResourceServer'), ['--config', file]);
        this.gate = `http://127.0.0.1:${ports.gate}`; this.resource = `http://127.0.0.1:${resource.port}`;
        for (const url of [`${this.gate}/get_test`, `${this.resource}/ready`]) {
            await poll(/** 等待真实 HTTP 服务就绪。 */ async () => {
                const result = await fetch(url, { signal: AbortSignal.timeout(1000) }); await result.text(); return result.ok;
            }, 30000);
        }
        await poll(/** 通过公开验证码链路核对 Varify 与真实 Redis/SMTP 就绪。 */ async () => {
            await post(`${this.gate}/get_varifycode`, { email: this.config.recipient }); return true;
        }, 30000);
    }
    /** 通过验证码与注册接口建立账号，密码只在内存中使用。 */
    async register(index) {
        this.signal?.throwIfAborted();
        const account = { email: `${this.config.runId}-${index}@example.invalid`,
            name: `perf${index}_${this.config.runId.slice(0, 8)}`, password: randomUUID().replaceAll('-', '').slice(0, 12) };
        await post(`${this.gate}/get_varifycode`, { email: account.email });
        let code;
        await poll(/** 只从本次临时收件人的邮件中提取验证码。 */ async () => {
            const found = await this.coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${account.email}`)}`);
            if (!found.messages.length) return false;
            const mail = await this.coordinator.mailApi(`/api/v1/message/${found.messages[0].ID}`);
            code = /验证码为([a-zA-Z0-9-]+)请/.exec(mail.Text)?.[1]; return Boolean(code);
        }, 10000);
        await post(`${this.gate}/user_register`, { email: account.email, user: account.name,
            passwd: account.password, confirm: account.password, varifycode: code });
        this.accounts.push(account); return account;
    }
    /** 在隔离生产组合中验证空闲 Gate、长资料重登、完整批量确认和 Redis 有效期。 */
    async auditRegressions() {
        // This wait exercises the real former sixty-second pre-accept deadline, not a readiness guess.
        await delay(61000, undefined, { signal: this.signal });
        const first = await this.register(-2);
        const second = await this.register(-1);
        let sender = await this.login(first);
        let receiver = await this.login(second);
        const chatId = await this.befriend(sender, receiver);
        const profile = await sender.request(1042, 1043, { request_id: randomUUID(),
            name: '名'.repeat(255), description: '述'.repeat(255), expected_revision: '1' });
        assert.equal(profile.error, 0);
        sender.close(); receiver.close();
        sender = await this.login(first); receiver = await this.login(second);
        const contacts = await receiver.request(1046, 1047, { request_id: randomUUID(), kind: 'contacts', after: '0' });
        assert.ok(contacts.items.some(/** 核对长字段完整保留。 */ item => item.uid === sender.uid && item.name === '名'.repeat(255)));
        const request = { from_uid: sender.uid, to_uid: receiver.uid, chat_id: chatId,
            text_array: Array.from({ length: 20 }, /** 形成合法但 ACK 大于旧上限的消息批次。 */ () => ({ msg_uuid: randomUUID(), msg_content: 'x' })) };
        const ack = await sender.request(1016, 1017, request);
        assert.equal(ack.uuid_msgId.length, 20);
        assert.deepEqual((await sender.request(1016, 1017, request)).uuid_msgId, ack.uuid_msgId);
        const redis = await this.coordinator.redis();
        try {
            assert.ok(await redis.ttl(String(sender.uid)) > 0, 'token-expiry');
            assert.ok(await redis.ttl(`usessionid_${sender.uid}`) > 0, 'presence-expiry');
            assert.equal(await redis.exists(`code_${first.email}`), 0, 'verification-consumed');
            for (const server of this.topology.servers) assert.ok(await redis.ttl(`chatlease_${server.name}`) > 0, 'instance-expiry');
        } finally { redis.disconnect(); sender.close(); receiver.close(); }
    }
    /** 经 Gate/Status 服务发现建立真实 TCP 会话，验证返回端点属于本次拓扑。 */
    async login(account) {
        this.signal?.throwIfAborted();
        const auth = await post(`${this.gate}/user_login`, { email: account.email, password: account.password });
        this.signal?.throwIfAborted();
        assert.equal(auth.host, '127.0.0.1');
        assert.ok(this.topology.servers.some(/** 核对真实选服结果。 */ server => server.port === Number(auth.port)));
        const client = new Client(Number(auth.port), auth.uid, auth.token);
        this.clients.push(client); await client.connect(); account.uid = auth.uid; account.client = client; return client;
    }
    /** 通过申请及接受接口建立双向好友，返回私聊身份。 */
    async befriend(first, second) {
        this.signal?.throwIfAborted();
        await first.request(1009, 1010, { fromuid: first.uid, touid: second.uid, applyname: 'performance',
            applydescription: 'fixture', applyicon: '', applysex: 0, description: 'fixture', backname: 'peer' });
        const accepted = await second.request(1013, 1014, { applyuid: first.uid, authuid: second.uid,
            applyinfo: { applyuid: first.uid, touid: second.uid, backname: 'peer', description: 'fixture' },
            authinfo: { authuid: second.uid, touid: first.uid, backname: 'peer', description: 'fixture' } });
        assert.ok(accepted.chatid > 0); return accepted.chatid;
    }
    /** 等待生产六十秒连接数发布后重新登录半数账号，避免把短时选服滞后伪作双实例负载。 */
    async balance(accounts) {
        if (new Set(accounts.map(/** 取实际登录端点。 */ account => account.client.port)).size === 2) return;
        const existing = accounts[0].client.port;
        const selected = this.topology.servers.find(/** 查找当前承载实例。 */ server => server.port === existing);
        const migrating = accounts.slice(accounts.length / 2);
        for (const account of migrating) account.client.close();
        await waitForConnectionCount(this.coordinator, selected.name, accounts.length - migrating.length, [], this.signal);
        for (const account of migrating) await this.login(account);
        assert.equal(new Set(accounts.map(/** 核对真实分配的端点。 */ account => account.client.port)).size, 2, 'two-live-instances');
    }
    /** 采样实际服务子进程的 Linux CPU 计数及 RSS，PID 来源于监督器身份。 */
    sample() {
        return this.children.map(/** 读取每个已登记服务的进程计数，不包含日志和环境。 */ owned => {
            const identity = JSON.parse(fs.readFileSync(`${owned.report}.identity`));
            const stat = fs.readFileSync(`/proc/${identity.pid}/stat`, 'utf8').split(') ')[1].split(' ');
            const status = fs.readFileSync(`/proc/${identity.pid}/status`, 'utf8');
            return { name: owned.name, pid: identity.pid, cpuTicks: Number(stat[11]) + Number(stat[12]),
                rssKiB: Number(/^VmRSS:\s+(\d+)/m.exec(status)?.[1] || 0) };
        });
    }
    /** 逆序清理服务与数据，所有失败分别保留，最后删除所属临时文件。 */
    async teardown(evidenceRoot) {
        const failures = [];
        for (const client of this.clients) client.close();
        for (const owned of [...this.children].reverse()) {
            try { await stop(owned); } catch { failures.push(`stop-${owned.name}`); }
        }
        for (const lease of this.leases) {
            if (!lease.released) { try { await lease.release(); } catch { failures.push('port-release'); } }
        }
        if (this.sql) { try { await this.sql.close(); } catch { failures.push('sql-close'); } }
        const dependencies = await this.coordinator.teardown();
        failures.push(...dependencies.failures);
        try { this.preserveEvidence(evidenceRoot); } catch { failures.push('evidence-export'); }
        try { fs.rmSync(this.root, { recursive: true, force: true }); } catch { failures.push('temporary-files'); }
        return { complete: failures.length === 0 && dependencies.complete, failures, dependencies };
    }
    /** 清理前保存有界脱敏日志及进程监督证据，配置和资源字节不进入工件。 */
    preserveEvidence(evidenceRoot) {
        const destination = path.join(evidenceRoot, 'diagnostics'); fs.mkdirSync(destination, { recursive: true });
        for (const owned of this.children) {
            for (const suffix of ['', '.identity']) {
                const source = owned.report + suffix;
                if (fs.existsSync(source)) fs.copyFileSync(source, path.join(destination, `${owned.name}${suffix}.json`));
            }
        }
        const logRoot = path.join(this.root, 'logs');
        if (!fs.existsSync(logRoot)) return;
        const secrets = [this.coordinator.password, ...this.accounts.flatMap(/** 汇集本次账号及会话凭据。 */ account =>
            [account.password, account.email, account.client?.token])].filter(Boolean);
        for (const name of fs.readdirSync(logRoot).slice(0, 20)) {
            const file = path.join(logRoot, name); const info = fs.lstatSync(file);
            if (!info.isFile() || info.isSymbolicLink()) continue;
            const handle = fs.openSync(file, 'r'); let bytes;
            try { bytes = Buffer.alloc(Math.min(info.size, 65536)); fs.readSync(handle, bytes, 0, bytes.length, Math.max(0, info.size - bytes.length)); }
            finally { fs.closeSync(handle); }
            let text = bytes.toString('utf8').split('\n').filter(/** 敏感协议与查询整行剔除，避免输出完整报文。 */ line =>
                !/token|passwd|password|验证码|varify|verifycode|SELECT |INSERT |UPDATE /i.test(line)).join('\n');
            for (const secret of secrets) text = text.replaceAll(secret, '<redacted>');
            fs.writeFileSync(path.join(destination, name), text);
        }
    }
}
module.exports = { Environment };
