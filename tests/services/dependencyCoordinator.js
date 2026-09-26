'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { createRequire } = require('node:module');
const { performance } = require('node:perf_hooks');
const { setTimeout: delay } = require('node:timers/promises');
const lock = require('./services.lock.json');
const { reportGroups, writeReports } = require('./serviceReports');

/** 携带允许公开的服务阶段和失败分类，不保存原始敏感错误。 */
class ServiceStepFailure extends Error {
    /** 以固定错误消息和已分类诊断构造服务步骤失败。 */
    constructor(stage, category) {
        super('service step failed');
        this.diagnostic = { stage, category };
    }
}

/** 从已知错误提取白名单诊断，未知错误不写入证据。 */
function caseDiagnostic(error) {
    if (error instanceof ServiceStepFailure) return error.diagnostic;
    const processFailure = /^FourProcess:(GateServer|StatusServer|ChatServer|VarifyServer):(stop-timeout|report-unavailable|stop-escalated|exit-[0-9]{1,10}|harness-incomplete|exit-unavailable|expected-failure)$/.exec(error.message);
    if (processFailure) return { stage: `stop-${processFailure[1]}`, category: processFailure[2] };
    const safeMysqlErrors = ['MysqlDeadlineExceeded', 'MysqlSessionUnavailable',
        'MysqlUnavailable', 'MysqlOutputLimit', 'MysqlSessionClosed'];
    if (safeMysqlErrors.includes(error.message) || /^MysqlError:[0-9]{1,5}$/.test(error.message)) {
        return { stage: 'service-case', category: error.message };
    }
    return undefined;
}

/** 执行初始化步骤，把原始异常映射为安全阶段和分类。 */
async function bootstrapStep(stage, action) {
    try { return await action(); }
    catch (error) {
        const knownCodes = ['ER_ACCESS_DENIED_ERROR', 'ECONNREFUSED', 'ETIMEDOUT', 'ERR_ASSERTION'];
        const knownMessages = ['health deadline', 'command deadline', 'command unavailable', 'command output limit'];
        const category = knownCodes.includes(error.code) ? error.code
            : knownMessages.includes(error.message) ? error.message.replaceAll(' ', '-') : 'operation-failed';
        throw new ServiceStepFailure(stage, category);
    }
}

/** 为业务合同子步骤保留固定阶段及数值错误码，始终丢弃原始错误内容。 */
async function contractStep(stage, action) {
    if (!/^(gate-(verify|mail|register|login|selection)|client-(create|chat-id|send|message-sync|message-data))$/.test(stage)) {
        throw new Error('Unknown contract diagnostic stage');
    }
    try { return await action(); }
    catch (error) {
        const category = error.name === 'TimeoutError' || error.name === 'AbortError' ? 'deadline'
            : error.code === 'ERR_ASSERTION' && Number.isInteger(error.actual) && Math.abs(error.actual) <= 2147483647
                ? `response-${error.actual}` : error.code === 'ERR_ASSERTION' ? 'assertion' : 'operation-failed';
        throw new ServiceStepFailure(stage, category);
    }
}

/** 校验隔离运行身份、loopback 端点与精确容器 ID，生成所属资源名称。 */
function loadConfiguration(env) {
    const port = /** 读取并严格校验十进制端口范围。 */ (name) => {
        const value = env[name] || '';
        if (!/^[0-9]+$/.test(value) || Number(value) < 1 || Number(value) > 65535) {
            throw new Error('invalid service configuration');
        }
        return Number(value);
    };
    if (!/^[a-f0-9]{32}$/.test(env.CHAT_SERVICE_RUN_ID || '') || env.CHAT_SERVICE_HOST !== '127.0.0.1') {
        throw new Error('invalid service configuration');
    }
    const ids = {};
    for (const service of Object.keys(lock.services)) {
        ids[service] = env[`CHAT_${service.toUpperCase()}_CONTAINER`];
        if (!/^[a-f0-9]{64}$/.test(ids[service] || '')) throw new Error('invalid service configuration');
    }
    const ports = {
        redis: port('CHAT_REDIS_PORT'), mysql: port('CHAT_MYSQL_PORT'),
        smtp: port('CHAT_SMTP_PORT'), mailpit: port('CHAT_MAILPIT_PORT')
    };
    if (new Set(Object.values(ports)).size !== 4) throw new Error('invalid service configuration');
    const runId = env.CHAT_SERVICE_RUN_ID;
    return { runId, ids, ports, host: env.CHAT_SERVICE_HOST, database: `chat_${runId}`,
        prefix: `chat:${runId}:`, recipient: `${runId}@example.invalid` };
}

// Never surface raw child errors/output: SQL input, SMTP responses and container
// metadata can contain secrets. Only successful bounded output reaches callers.
/** 有界运行命令并限制输出，非零退出或启动失败均返回脱敏错误。 */
function runCommand(executable, args, options = {}) {
    return new Promise(/** 拥有子进程与截止计时器，等待退出后统一处理结果。 */ (resolve, reject) => {
        const child = spawn(executable, args, {
            env: options.env || process.env, stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true
        });
        let output = '';
        let failure;
        const timer = setTimeout(/** 命令超时后记录失败并强制终止所属子进程。 */ () => {
            failure = new Error('command deadline');
            child.kill('SIGKILL');
        }, options.timeout || lock.commandDeadlineMs);
        child.on('error', /** 记录无法启动命令的固定诊断。 */ () => { failure = new Error('command unavailable'); });
        child.stdout.on('data', /** 累计标准输出，超限则终止进程并报告输出限制。 */ (chunk) => {
            if (output.length + chunk.length > 65536) {
                failure = new Error('command output limit');
                child.kill('SIGKILL');
            } else output += chunk.toString();
        });
        child.stderr.resume();
        child.stdin.on('error', /** 输入管道关闭由进程退出统一判定，避免未处理错误事件。 */ () => { /* Exit status owns closed-input failure. */ });
        child.on('close', /** 取消截止计时器并按真实退出结果交付输出或错误。 */ (code) => {
            clearTimeout(timer);
            if (failure) reject(failure);
            else if (code !== 0) reject(new Error(`command failed (${code})`));
            else resolve(output.trim());
        });
        child.stdin.end(options.input || '');
    });
}

/** 在单调截止时间内轮询异步探针，临时失败可重试，到期抛出健康检查失败。 */
async function poll(probe, timeoutMs) {
    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
        try { if (await probe()) return; } catch { /* Recheck bounded dependency readiness. */ }
        const remaining = deadline - performance.now();
        if (remaining > 0) await delay(Math.min(100, remaining));
    }
    throw new Error('health deadline');
}

/** 连接 TCP 端点并有界等待首批握手数据，任一终态都销毁套接字。 */
function tcpHandshake(host, port, timeout = 1000) {
    return new Promise(/** 注册首包、超时、错误与提前关闭事件以完成握手等待。 */ (resolve, reject) => {
        const socket = net.createConnection({ host, port });
        socket.setTimeout(timeout);
        socket.once('data', /** 收到首批握手数据后关闭连接并交付字节。 */ (data) => { socket.destroy(); resolve(data); });
        socket.once('timeout', /** 握手超时后关闭连接并报告期限到达。 */ () => { socket.destroy(); reject(new Error('connection deadline')); });
        socket.once('error', /** 连接错误后关闭套接字并报告拒绝。 */ () => { socket.destroy(); reject(new Error('connection rejected')); });
        socket.once('end', /** 对端未发送数据即结束时关闭连接并报告提前结束。 */ () => { socket.destroy(); reject(new Error('connection ended')); });
    });
}

/** 验证并管理本次 Docker 依赖的身份、凭据、数据和生命周期。 */
class DependencyCoordinator {
    /** 保存运行配置，生成临时凭据并装配可注入的外部客户端。 */
    constructor(config, dependencies = {}) {
        this.config = config;
        this.password = crypto.randomBytes(32).toString('hex');
        this.mailBody = crypto.randomBytes(24).toString('hex');
        this.owned = new Set();
        this.passwordSet = false;
        this.databaseCreated = false;
        this.messages = [];
        this.clients = new Set();
        const requireVarify = createRequire(path.resolve(__dirname, '../../VarifyServer/package.json'));
        this.Redis = dependencies.Redis || requireVarify('ioredis');
        this.nodemailer = dependencies.nodemailer || requireVarify('nodemailer');
        this.runCommand = dependencies.runCommand || runCommand;
    }

    /** 核对容器 ID、镜像、运行状态和端口；仅已验证容器允许刷新重启后的端点。 */
    async inspect(service, refreshPorts = false) {
        if (refreshPorts) assert.ok(this.owned.has(service), 'refusing unverified container');
        const result = await this.runCommand('docker', ['inspect', this.config.ids[service]]);
        const containers = JSON.parse(result);
        assert.equal(containers.length, 1);
        const [container] = containers;
        assert.equal(container.Id, this.config.ids[service]);
        assert.equal(container.Config.Image, lock.services[service].image);
        assert.equal(container.State.Running, true);
        const ports = { ...this.config.ports };
        for (const port of lock.services[service].ports) {
            const bindings = container.NetworkSettings.Ports[`${port}/tcp`];
            assert.ok(bindings && bindings.length === 1);
            assert.equal(bindings[0].HostIp, '127.0.0.1');
            const key = port === 1025 ? 'smtp' : service;
            assert.match(bindings[0].HostPort, /^[0-9]+$/);
            const hostPort = Number(bindings[0].HostPort);
            assert.ok(hostPort >= 1 && hostPort <= 65535);
            if (!refreshPorts) assert.equal(hostPort, this.config.ports[key]);
            ports[key] = hostPort;
        }
        assert.equal(new Set(Object.values(ports)).size, Object.keys(ports).length);
        // Docker may reassign ephemeral host ports on start. Publish all mappings
        // only after identity and every loopback binding pass, preserving consumers.
        Object.assign(this.config.ports, ports);
        this.owned.add(service);
        return container;
    }

    /** 只启动或停止已验证的所属容器，重启后重新校验并刷新端口。 */
    async lifecycle(service, operation) {
        assert.ok(this.owned.has(service), 'refusing unverified container');
        assert.ok(operation === 'start' || operation === 'stop');
        const args = operation === 'stop' ? ['stop', '--time', '10'] : ['start'];
        await bootstrapStep(`${service}-${operation}`, /** 按精确容器 ID 执行有界启动或停止命令。 */ () =>
            this.runCommand('docker', [...args, this.config.ids[service]], { timeout: 15000 }));
        if (operation === 'start') await bootstrapStep(`${service}-restart-endpoints`, /** 重启后重新验证容器与动态端口映射。 */ () => this.inspect(service, true));
    }

    /** 创建无离线队列和自动重试的有界 Redis 连接，失败时释放并移除记录。 */
    async redis(password = this.password) {
        const client = new this.Redis({ host: this.config.host, port: this.config.ports.redis,
            password, lazyConnect: true, connectTimeout: 1000, commandTimeout: 1000,
            retryStrategy: null, maxRetriesPerRequest: 0, enableOfflineQueue: false });
        client.on('error', /** Redis 错误由调用 Promise 传播，事件处理不记录敏感信息。 */ () => { /* Call promises report safe failures. */ });
        this.clients.add(client);
        try { await client.connect(); return client; }
        catch (error) { client.disconnect(); this.clients.delete(client); throw error; }
    }

    /** 仅对已验证的 MySQL 容器通过本地 socket 执行 SQL，凭据和语句不进入命令行。 */
    async sql(statement, password = this.passwordSet ? this.password : '') {
        assert.ok(this.owned.has('mysql'));
        // Socket pins administration to root@localhost even with skip-name-resolve.
        // Docker inherits MYSQL_PWD by name; neither password nor SQL is argv.
        return this.runCommand('docker', ['exec', '-i', '--env', 'MYSQL_PWD', this.config.ids.mysql,
            'mysql', '--protocol=SOCKET', '--host=localhost', '--user=root', '--connect-timeout=2',
            '--batch', '--skip-column-names', '--silent'], {
            env: { ...process.env, MYSQL_PWD: password }, input: statement
        });
    }

    /** 有界调用所属 Mailpit HTTP 接口，拒绝重定向并核对成功状态。 */
    async mailApi(route, options = {}) {
        const response = await fetch(`http://${this.config.host}:${this.config.ports.mailpit}${route}`, {
            ...options, signal: AbortSignal.timeout(2000), redirect: 'error'
        });
        if (!response.ok) throw new Error('mail API failed');
        if (route === '/readyz' || options.method === 'DELETE') { await response.text(); return true; }
        return response.json();
    }

    /** 创建仅连接本次 SMTP 端点的有界邮件传输器，由调用者关闭。 */
    smtp() {
        return this.nodemailer.createTransport({ host: this.config.host, port: this.config.ports.smtp,
            secure: false, ignoreTLS: true, connectionTimeout: 1000, greetingTimeout: 1000,
            socketTimeout: 2000, logger: false, debug: false });
    }

    /** 等待真实依赖就绪，再设置本次 MySQL、Redis 凭据并核对 Mailpit。 */
    async bootstrap() {
        const deadline = performance.now() + lock.healthDeadlineMs;
        const remaining = /** 计算扣除命令预留时间后的剩余健康检查预算。 */ () => Math.max(1, deadline - performance.now() - lock.commandDeadlineMs);
        // The official image's temporary initialization server disables networking.
        // Wait for the final server before changing credentials over its local socket.
        await bootstrapStep('mysql-network-ready', /** 在剩余预算内等待 MySQL 正式网络服务的握手。 */ () => poll(/** 检查 TCP 首包中的 MySQL 协议版本。 */ async () =>
            (await tcpHandshake(this.config.host, this.config.ports.mysql))[4] === 10, remaining()));
        await bootstrapStep('mysql-local-ready', /** 在剩余预算内等待 MySQL 本地管理查询成功。 */ () => poll(/** 核对本地查询返回预期标量。 */ async () =>
            (await this.sql('SELECT 1;')) === '1', remaining()));
        await bootstrapStep('mysql-account', /** 确认当前管理连接实际使用 root 本地账号。 */ async () => {
            assert.equal(await this.sql('SELECT CURRENT_USER();'), 'root@localhost');
        });
        await bootstrapStep('mysql-local-password', /** 为所属 MySQL 的本地管理账号设置本次临时密码。 */ () =>
            this.sql(`ALTER USER 'root'@'localhost' IDENTIFIED BY '${this.password}';`));
        this.passwordSet = true;
        // The official image can create a second root account for network peers.
        // Protect every bootstrap root account before any fixture data is written.
        const hosts = (await bootstrapStep('mysql-root-hosts', /** 查询所属 MySQL 中 root 账号的主机范围。 */ () =>
            this.sql("SELECT Host FROM mysql.user WHERE User='root';"))).split('\n');
        for (const host of hosts) {
            await bootstrapStep('mysql-peer-password', /** 只接受预期 root 主机范围，并同步非本地账号临时密码。 */ async () => {
                assert.ok(host === 'localhost' || host === '%');
                if (host !== 'localhost') await this.sql(`ALTER USER 'root'@'${host}' IDENTIFIED BY '${this.password}';`);
            });
        }
        await bootstrapStep('redis-ready', /** 在剩余预算内轮询 Redis 无密码初始就绪。 */ () => poll(/** 通过 PING 核对 Redis 就绪，结束后断开探针连接。 */ async () => {
            const client = await this.redis(null);
            try { return (await client.ping()) === 'PONG'; } finally { client.disconnect(); }
        }, remaining()));
        await bootstrapStep('redis-password', /** 为本次 Redis 设置临时认证密码并关闭配置连接。 */ async () => {
            const client = await this.redis(null);
            try { await client.config('SET', 'requirepass', this.password); } finally { client.disconnect(); }
        });
        await bootstrapStep('mailpit-ready', /** 在剩余预算内轮询 Mailpit 就绪接口。 */ () => poll(/** 探测本次 Mailpit 的 readyz 接口。 */ () => this.mailApi('/readyz'), remaining()));
    }

    /** 定向清理本次邮件、证明键和数据库，独立收集各项失败。 */
    async clearData() {
        const failures = [];
        const attempt = /** 尝试单项清理，失败时保留其资源名并继续其他清理。 */ async (name, action) => {
            try { await action(); } catch { failures.push(name); }
        };
        if (this.owned.has('mailpit')) await attempt('mail', /** 只删除本次收件人的邮件并复查没有残留。 */ async () => {
            const found = await this.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${this.config.recipient}`)}`);
            const ids = found.messages.map(/** 提取所属邮件的精确 ID 供定向删除。 */ (message) => message.ID);
            if (ids.length) await this.mailApi('/api/v1/messages', {
                method: 'DELETE', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ IDs: ids })
            });
            const remaining = await this.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${this.config.recipient}`)}`);
            assert.equal(remaining.messages.length, 0);
        });
        if (this.owned.has('redis')) await attempt('redis', /** 删除本次唯一证明键并确认不存在，不清空其他 Redis 数据。 */ async () => {
            const client = await this.redis();
            try {
                // Only this fixture's exact key is owned, never FLUSHDB or broad scan/delete.
                await client.del(`${this.config.prefix}proof`);
                assert.equal(await client.exists(`${this.config.prefix}proof`), 0);
            } finally { client.disconnect(); }
        });
        if (this.databaseCreated) await attempt('mysql', /** 删除本次创建的数据库并复查不存在，然后撤销创建标志。 */ async () => {
            await this.sql(`DROP DATABASE IF EXISTS \`${this.config.database}\`;`);
            assert.equal(await this.sql(`SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${this.config.database}';`), '0');
            this.databaseCreated = false;
        });
        return failures;
    }

    /** 清理所属数据与客户端，停止全部已验证容器并报告完整清理结果。 */
    async teardown() {
        const failures = await this.clearData();
        for (const client of this.clients) client.disconnect();
        const stopped = [];
        for (const service of ['mailpit', 'mysql', 'redis']) {
            if (!this.owned.has(service)) continue;
            try {
                await this.lifecycle(service, 'stop');
                const [container] = JSON.parse(await this.runCommand('docker', ['inspect', this.config.ids[service]]));
                assert.equal(container.State.Running, false);
                stopped.push(service);
            } catch { failures.push(`stop-${service}`); }
        }
        return { complete: failures.length === 0 && stopped.length === 3, failures, stopped };
    }
}

/** 运行所选真实依赖合同，聚合用例、脱敏证据与清理结果并传播失败。 */
async function runSuite(evidenceRoot) {
    fs.mkdirSync(evidenceRoot, { recursive: true });
    const selector = process.env.CHAT_SERVICE_SELECTOR || '3C-02';
    const groups = reportGroups(selector);
    const cases = [];
    let coordinator;
    let config;
    let primaryFailure = null;
    let cleanup = { complete: false, failures: ['not-started'], stopped: [] };
    const record = /** 记录用例真实结果、耗时及允许公开的诊断，失败后中断当前流程。 */ async (id, name, action) => {
        const start = performance.now();
        try { await action(); cases.push({ id, name, pass: true, seconds: (performance.now() - start) / 1000 }); }
        catch (error) {
            cases.push({ id, name, pass: false,
                diagnostic: caseDiagnostic(error),
                seconds: (performance.now() - start) / 1000 });
            throw new Error(id);
        }
    };
    try {
        config = loadConfiguration(process.env);
        coordinator = new DependencyCoordinator(config);
        await record('T10-SVC-01', 'owned mapped endpoints', /** 逐个核对锁文件规定的依赖容器身份与端点。 */ async () => {
            for (const service of Object.keys(lock.services)) await coordinator.inspect(service);
        });
        await record('T10-SVC-02', 'bounded bootstrap and synthetic authentication', /** 初始化依赖并核对 MySQL、Redis 认证连接确实可用。 */ async () => {
            await coordinator.bootstrap();
            await bootstrapStep('mysql-authenticated', /** 核对认证后的 MySQL 标量查询成功。 */ async () => {
                assert.equal(await coordinator.sql('SELECT 1;'), '1');
            });
            await bootstrapStep('redis-authenticated', /** 核对认证后的 Redis PING 成功，并释放连接。 */ async () => {
                const client = await coordinator.redis();
                try { assert.equal(await client.ping(), 'PONG'); } finally { client.disconnect(); }
            });
        });
        await record('T10-SVC-03', 'isolated Redis data', /** 写入带 TTL 的运行证明键并读回核对。 */ async () => {
            const client = await coordinator.redis();
            try {
                await client.set(`${config.prefix}proof`, config.runId, 'EX', 120);
                assert.equal(await client.get(`${config.prefix}proof`), config.runId);
            } finally { client.disconnect(); }
        });
        await record('T10-SVC-04', 'MySQL mapped handshake and owned database', /** 核对 MySQL 握手，创建独立数据库并验证当前 schema。 */ async () => {
            const handshake = await tcpHandshake(config.host, config.ports.mysql);
            assert.equal(handshake[4], 10);
            await coordinator.sql(`CREATE DATABASE \`${config.database}\`;`);
            coordinator.databaseCreated = true;
            assert.equal(await coordinator.sql(`USE \`${config.database}\`; SELECT DATABASE();`), config.database);
        });
        if (groups.some(/** 判断所选报告是否要求 schema 迁移合同。 */ (group) => group.prefix === 'T10-MIG-')) {
            await require('../server/schema-migration/runMigrationCases').runMigrationCases(coordinator, record);
        }
        if (groups.some(/** 判断所选报告是否要求消息持久化合同。 */ (group) => group.prefix === 'T10-MSG-')) {
            await require('../server/message-commit/runMessageCases').runMessageCases(coordinator, record);
        }
        await record('T10-SVC-05', 'Mailpit SMTP and API correlation', /** 通过真实 SMTP 发送合成邮件，并等待 Mailpit 中唯一对应收件记录。 */ async () => {
            const transport = coordinator.smtp();
            try {
                await transport.verify();
                await transport.sendMail({ from: 'proof@example.invalid', to: config.recipient,
                    subject: config.runId, text: coordinator.mailBody });
            } finally { transport.close(); }
            await poll(/** 检查本次收件人恰有一封证明邮件。 */ async () => {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${config.recipient}`)}`);
                return found.messages.length === 1;
            }, 5000);
        });
        if (groups.some(/** 判断所选报告是否要求验证码 SMTP 适配器合同。 */ (group) => group.prefix === 'V09-SMTP-')) {
            await require('../../VarifyServer/test/smtp/mailpit-suite').runSmtpCases(coordinator, record);
        }
        if (groups.some(/** 判断所选报告是否要求原生 Redis 适配器合同。 */ (group) => group.prefix === 'T10-RDS-')) {
            await require('../server/data/runRedisCases').runRedisCases(coordinator, record);
            await require('../../VarifyServer/test/redis/redis-suite').runRedisCases(coordinator, record);
        }
        if (groups.some(/** 判断所选报告是否要求四进程真实服务合同。 */ (group) => group.prefix === 'T10-4PROC-')) {
            await require('./fourProcessCases').runFourProcessCases(coordinator, record);
        }
        await record('T10-SVC-06', 'real unavailable health deadline', /** 停止 Mailpit 验证健康探测失败，再恢复并验证健康检查通过。 */ async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            try {
                await bootstrapStep('mailpit-unavailable-health', /** 要求已停止 Mailpit 的有界健康检查失败。 */ () =>
                    assert.rejects(poll(/** 探测已停止的 Mailpit 就绪接口。 */ () => coordinator.mailApi('/readyz'), 500), /health deadline/));
            }
            finally { await coordinator.lifecycle('mailpit', 'start'); }
            await bootstrapStep('mailpit-health-recovery', /** 轮询重启后的 Mailpit 直到就绪。 */ () => poll(/** 探测恢复后的 Mailpit 就绪接口。 */ () => coordinator.mailApi('/readyz'), 5000));
        });
        await record('T10-SVC-07', 'stopped SMTP connection failure', /** 停止 Mailpit 验证 SMTP 失败，并在最终路径关闭传输器和恢复服务。 */ async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            const transport = coordinator.smtp();
            try { await bootstrapStep('mailpit-unavailable-smtp', /** 要求已停止的 SMTP 连接校验失败。 */ () => assert.rejects(transport.verify())); }
            finally { transport.close(); await coordinator.lifecycle('mailpit', 'start'); }
            await bootstrapStep('mailpit-smtp-recovery', /** 轮询 SMTP 故障后恢复的 Mailpit 健康状态。 */ () => poll(/** 探测 SMTP 恢复后的 Mailpit 就绪接口。 */ () => coordinator.mailApi('/readyz'), 5000));
        });
        await record('T10-SVC-08', 'incorrect credentials rejected', /** 验证随机错误密码均被 MySQL 和 Redis 拒绝。 */ async () => {
            await assert.rejects(coordinator.sql('SELECT 1;', crypto.randomBytes(32).toString('hex')));
            await assert.rejects(coordinator.redis(crypto.randomBytes(32).toString('hex')));
        });
        await record('T10-SVC-09', 'Redis stop breaks real connection', /** 停止 Redis 并验证新连接失败。 */ async () => {
            await coordinator.lifecycle('redis', 'stop');
            await assert.rejects(coordinator.redis());
        });
        await record('T10-SVC-10', 'cleanup failure preserved and dependency recovery', /** 验证 Redis 不可用时清理失败可见，随后恢复依赖认证。 */ async () => {
            assert.ok((await coordinator.clearData()).includes('redis'));
            await coordinator.lifecycle('redis', 'start');
            await bootstrapStep('redis-restart-authentication', /** 轮询重启 Redis 并重新配置临时认证密码。 */ () => poll(/** 设置恢复后的 Redis 认证并释放配置连接。 */ async () => {
                const client = await coordinator.redis(null);
                try { await client.config('SET', 'requirepass', coordinator.password); return true; }
                finally { client.disconnect(); }
            }, 5000));
        });
    } catch (error) { primaryFailure = /^(T10-(SVC|RDS|MIG|MSG|4PROC)|V0[89]-(REDIS|SMTP))-[0-9]+$/.test(error.message) ? error.message : 'setup'; }
    finally {
        if (coordinator) cleanup = await coordinator.teardown();
        cases.push({ id: 'T10-SVC-11', name: 'owned data and service teardown', pass: cleanup.complete, seconds: 0 });
        const endpoints = config ? { runId: config.runId, host: config.host, ports: config.ports,
            database: config.database, prefix: config.prefix, recipient: config.recipient,
            images: Object.fromEntries(Object.entries(lock.services).map(/** 提取锁文件中的服务与固定镜像映射供证据记录。 */ ([key, value]) => [key, value.image])) } : { status: 'setup-failed' };
        const teardown = { runId: config?.runId || null, primaryFailure,
            diagnostics: cases.filter(/** 筛选含安全诊断的失败用例。 */ (entry) => entry.diagnostic).map(/** 把用例编号与安全诊断组合为证据记录。 */ ({ id, diagnostic }) => ({ id, ...diagnostic })),
            ...cleanup };
        const evidence = JSON.stringify({ endpoints, teardown, cases });
        const redacted = !coordinator || ![coordinator.password, coordinator.mailBody].some(/** 检查编码证据是否泄漏本次敏感值。 */ (secret) => evidence.includes(secret));
        cases.push({ id: 'T10-SVC-12', name: 'evidence excludes generated secrets and body', pass: redacted, seconds: 0 });
        fs.writeFileSync(path.join(evidenceRoot, 'service-endpoints.json'), JSON.stringify(endpoints, null, 2));
        fs.writeFileSync(path.join(evidenceRoot, 'teardown.json'), JSON.stringify(teardown, null, 2));
        fs.writeFileSync(path.join(evidenceRoot, 'redaction.json'), JSON.stringify({ complete: redacted,
            scope: 'coordinator-generated-credentials-and-mail-body' }));
        writeReports(evidenceRoot, selector, cases);
    }
    if (primaryFailure || !cleanup.complete || cases.some(/** 识别未通过用例以决定整体退出结果。 */ (entry) => !entry.pass)) throw new Error('services proof failed');
}

module.exports = { DependencyCoordinator, loadConfiguration, poll, runCommand, runSuite, caseDiagnostic, contractStep };
if (require.main === module) {
    runSuite(process.env.CHAT_SERVICE_EVIDENCE_ROOT).catch(/** 主流程失败时输出有界证据定位提示并设置非零退出码。 */ () => {
        process.stderr.write('services proof failed; inspect bounded service evidence\n');
        process.exitCode = 1;
    });
}
