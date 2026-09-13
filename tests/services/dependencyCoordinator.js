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

class ServiceStepFailure extends Error {
    constructor(stage, category) {
        super('service step failed');
        this.diagnostic = { stage, category };
    }
}

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

function loadConfiguration(env) {
    const port = (name) => {
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
function runCommand(executable, args, options = {}) {
    return new Promise((resolve, reject) => {
        const child = spawn(executable, args, {
            env: options.env || process.env, stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true
        });
        let output = '';
        let failure;
        const timer = setTimeout(() => {
            failure = new Error('command deadline');
            child.kill('SIGKILL');
        }, options.timeout || lock.commandDeadlineMs);
        child.on('error', () => { failure = new Error('command unavailable'); });
        child.stdout.on('data', (chunk) => {
            if (output.length + chunk.length > 65536) {
                failure = new Error('command output limit');
                child.kill('SIGKILL');
            } else output += chunk.toString();
        });
        child.stderr.resume();
        child.stdin.on('error', () => { /* Exit status owns closed-input failure. */ });
        child.on('close', (code) => {
            clearTimeout(timer);
            if (failure) reject(failure);
            else if (code !== 0) reject(new Error(`command failed (${code})`));
            else resolve(output.trim());
        });
        child.stdin.end(options.input || '');
    });
}

async function poll(probe, timeoutMs) {
    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
        try { if (await probe()) return; } catch { /* Recheck bounded dependency readiness. */ }
        const remaining = deadline - performance.now();
        if (remaining > 0) await delay(Math.min(100, remaining));
    }
    throw new Error('health deadline');
}

function tcpHandshake(host, port, timeout = 1000) {
    return new Promise((resolve, reject) => {
        const socket = net.createConnection({ host, port });
        socket.setTimeout(timeout);
        socket.once('data', (data) => { socket.destroy(); resolve(data); });
        socket.once('timeout', () => { socket.destroy(); reject(new Error('connection deadline')); });
        socket.once('error', () => { socket.destroy(); reject(new Error('connection rejected')); });
        socket.once('end', () => { socket.destroy(); reject(new Error('connection ended')); });
    });
}

class DependencyCoordinator {
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

    async lifecycle(service, operation) {
        assert.ok(this.owned.has(service), 'refusing unverified container');
        assert.ok(operation === 'start' || operation === 'stop');
        const args = operation === 'stop' ? ['stop', '--time', '10'] : ['start'];
        await bootstrapStep(`${service}-${operation}`, () =>
            this.runCommand('docker', [...args, this.config.ids[service]], { timeout: 15000 }));
        if (operation === 'start') await bootstrapStep(`${service}-restart-endpoints`, () => this.inspect(service, true));
    }

    async redis(password = this.password) {
        const client = new this.Redis({ host: this.config.host, port: this.config.ports.redis,
            password, lazyConnect: true, connectTimeout: 1000, commandTimeout: 1000,
            retryStrategy: null, maxRetriesPerRequest: 0, enableOfflineQueue: false });
        client.on('error', () => { /* Call promises report safe failures. */ });
        this.clients.add(client);
        try { await client.connect(); return client; }
        catch (error) { client.disconnect(); this.clients.delete(client); throw error; }
    }

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

    async mailApi(route, options = {}) {
        const response = await fetch(`http://${this.config.host}:${this.config.ports.mailpit}${route}`, {
            ...options, signal: AbortSignal.timeout(2000), redirect: 'error'
        });
        if (!response.ok) throw new Error('mail API failed');
        if (route === '/readyz' || options.method === 'DELETE') { await response.text(); return true; }
        return response.json();
    }

    smtp() {
        return this.nodemailer.createTransport({ host: this.config.host, port: this.config.ports.smtp,
            secure: false, ignoreTLS: true, connectionTimeout: 1000, greetingTimeout: 1000,
            socketTimeout: 2000, logger: false, debug: false });
    }

    async bootstrap() {
        const deadline = performance.now() + lock.healthDeadlineMs;
        const remaining = () => Math.max(1, deadline - performance.now() - lock.commandDeadlineMs);
        // The official image's temporary initialization server disables networking.
        // Wait for the final server before changing credentials over its local socket.
        await bootstrapStep('mysql-network-ready', () => poll(async () =>
            (await tcpHandshake(this.config.host, this.config.ports.mysql))[4] === 10, remaining()));
        await bootstrapStep('mysql-local-ready', () => poll(async () =>
            (await this.sql('SELECT 1;')) === '1', remaining()));
        await bootstrapStep('mysql-account', async () => {
            assert.equal(await this.sql('SELECT CURRENT_USER();'), 'root@localhost');
        });
        await bootstrapStep('mysql-local-password', () =>
            this.sql(`ALTER USER 'root'@'localhost' IDENTIFIED BY '${this.password}';`));
        this.passwordSet = true;
        // The official image can create a second root account for network peers.
        // Protect every bootstrap root account before any fixture data is written.
        const hosts = (await bootstrapStep('mysql-root-hosts', () =>
            this.sql("SELECT Host FROM mysql.user WHERE User='root';"))).split('\n');
        for (const host of hosts) {
            await bootstrapStep('mysql-peer-password', async () => {
                assert.ok(host === 'localhost' || host === '%');
                if (host !== 'localhost') await this.sql(`ALTER USER 'root'@'${host}' IDENTIFIED BY '${this.password}';`);
            });
        }
        await bootstrapStep('redis-ready', () => poll(async () => {
            const client = await this.redis(null);
            try { return (await client.ping()) === 'PONG'; } finally { client.disconnect(); }
        }, remaining()));
        await bootstrapStep('redis-password', async () => {
            const client = await this.redis(null);
            try { await client.config('SET', 'requirepass', this.password); } finally { client.disconnect(); }
        });
        await bootstrapStep('mailpit-ready', () => poll(() => this.mailApi('/readyz'), remaining()));
    }

    async clearData() {
        const failures = [];
        const attempt = async (name, action) => {
            try { await action(); } catch { failures.push(name); }
        };
        if (this.owned.has('mailpit')) await attempt('mail', async () => {
            const found = await this.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${this.config.recipient}`)}`);
            const ids = found.messages.map((message) => message.ID);
            if (ids.length) await this.mailApi('/api/v1/messages', {
                method: 'DELETE', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ IDs: ids })
            });
            const remaining = await this.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${this.config.recipient}`)}`);
            assert.equal(remaining.messages.length, 0);
        });
        if (this.owned.has('redis')) await attempt('redis', async () => {
            const client = await this.redis();
            try {
                // Only this fixture's exact key is owned, never FLUSHDB or broad scan/delete.
                await client.del(`${this.config.prefix}proof`);
                assert.equal(await client.exists(`${this.config.prefix}proof`), 0);
            } finally { client.disconnect(); }
        });
        if (this.databaseCreated) await attempt('mysql', async () => {
            await this.sql(`DROP DATABASE IF EXISTS \`${this.config.database}\`;`);
            assert.equal(await this.sql(`SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${this.config.database}';`), '0');
            this.databaseCreated = false;
        });
        return failures;
    }

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

async function runSuite(evidenceRoot) {
    fs.mkdirSync(evidenceRoot, { recursive: true });
    const selector = process.env.CHAT_SERVICE_SELECTOR || '3C-02';
    const groups = reportGroups(selector);
    const cases = [];
    let coordinator;
    let config;
    let primaryFailure = null;
    let cleanup = { complete: false, failures: ['not-started'], stopped: [] };
    const record = async (id, name, action) => {
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
        await record('T10-SVC-01', 'owned mapped endpoints', async () => {
            for (const service of Object.keys(lock.services)) await coordinator.inspect(service);
        });
        await record('T10-SVC-02', 'bounded bootstrap and synthetic authentication', async () => {
            await coordinator.bootstrap();
            await bootstrapStep('mysql-authenticated', async () => {
                assert.equal(await coordinator.sql('SELECT 1;'), '1');
            });
            await bootstrapStep('redis-authenticated', async () => {
                const client = await coordinator.redis();
                try { assert.equal(await client.ping(), 'PONG'); } finally { client.disconnect(); }
            });
        });
        await record('T10-SVC-03', 'isolated Redis data', async () => {
            const client = await coordinator.redis();
            try {
                await client.set(`${config.prefix}proof`, config.runId, 'EX', 120);
                assert.equal(await client.get(`${config.prefix}proof`), config.runId);
            } finally { client.disconnect(); }
        });
        await record('T10-SVC-04', 'MySQL mapped handshake and owned database', async () => {
            const handshake = await tcpHandshake(config.host, config.ports.mysql);
            assert.equal(handshake[4], 10);
            await coordinator.sql(`CREATE DATABASE \`${config.database}\`;`);
            coordinator.databaseCreated = true;
            assert.equal(await coordinator.sql(`USE \`${config.database}\`; SELECT DATABASE();`), config.database);
        });
        if (groups.some((group) => group.prefix === 'T10-MIG-')) {
            await require('../server/schema-migration/runMigrationCases').runMigrationCases(coordinator, record);
        }
        if (groups.some((group) => group.prefix === 'T10-MSG-')) {
            await require('../server/message-commit/runMessageCases').runMessageCases(coordinator, record);
        }
        await record('T10-SVC-05', 'Mailpit SMTP and API correlation', async () => {
            const transport = coordinator.smtp();
            try {
                await transport.verify();
                await transport.sendMail({ from: 'proof@example.invalid', to: config.recipient,
                    subject: config.runId, text: coordinator.mailBody });
            } finally { transport.close(); }
            await poll(async () => {
                const found = await coordinator.mailApi(`/api/v1/search?query=${encodeURIComponent(`to:${config.recipient}`)}`);
                return found.messages.length === 1;
            }, 5000);
        });
        if (groups.some((group) => group.prefix === 'V09-SMTP-')) {
            await require('../../VarifyServer/test/smtp/mailpit-suite').runSmtpCases(coordinator, record);
        }
        if (groups.some((group) => group.prefix === 'T10-RDS-')) {
            await require('../server/data/runRedisCases').runRedisCases(coordinator, record);
            await require('../../VarifyServer/test/redis/redis-suite').runRedisCases(coordinator, record);
        }
        if (groups.some((group) => group.prefix === 'T10-4PROC-')) {
            await require('./fourProcessCases').runFourProcessCases(coordinator, record);
        }
        await record('T10-SVC-06', 'real unavailable health deadline', async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            try {
                await bootstrapStep('mailpit-unavailable-health', () =>
                    assert.rejects(poll(() => coordinator.mailApi('/readyz'), 500), /health deadline/));
            }
            finally { await coordinator.lifecycle('mailpit', 'start'); }
            await bootstrapStep('mailpit-health-recovery', () => poll(() => coordinator.mailApi('/readyz'), 5000));
        });
        await record('T10-SVC-07', 'stopped SMTP connection failure', async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            const transport = coordinator.smtp();
            try { await bootstrapStep('mailpit-unavailable-smtp', () => assert.rejects(transport.verify())); }
            finally { transport.close(); await coordinator.lifecycle('mailpit', 'start'); }
            await bootstrapStep('mailpit-smtp-recovery', () => poll(() => coordinator.mailApi('/readyz'), 5000));
        });
        await record('T10-SVC-08', 'incorrect credentials rejected', async () => {
            await assert.rejects(coordinator.sql('SELECT 1;', crypto.randomBytes(32).toString('hex')));
            await assert.rejects(coordinator.redis(crypto.randomBytes(32).toString('hex')));
        });
        await record('T10-SVC-09', 'Redis stop breaks real connection', async () => {
            await coordinator.lifecycle('redis', 'stop');
            await assert.rejects(coordinator.redis());
        });
        await record('T10-SVC-10', 'cleanup failure preserved and dependency recovery', async () => {
            assert.ok((await coordinator.clearData()).includes('redis'));
            await coordinator.lifecycle('redis', 'start');
            await bootstrapStep('redis-restart-authentication', () => poll(async () => {
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
            images: Object.fromEntries(Object.entries(lock.services).map(([key, value]) => [key, value.image])) } : { status: 'setup-failed' };
        const teardown = { runId: config?.runId || null, primaryFailure,
            diagnostics: cases.filter((entry) => entry.diagnostic).map(({ id, diagnostic }) => ({ id, ...diagnostic })),
            ...cleanup };
        const evidence = JSON.stringify({ endpoints, teardown, cases });
        const redacted = !coordinator || ![coordinator.password, coordinator.mailBody].some((secret) => evidence.includes(secret));
        cases.push({ id: 'T10-SVC-12', name: 'evidence excludes generated secrets and body', pass: redacted, seconds: 0 });
        fs.writeFileSync(path.join(evidenceRoot, 'service-endpoints.json'), JSON.stringify(endpoints, null, 2));
        fs.writeFileSync(path.join(evidenceRoot, 'teardown.json'), JSON.stringify(teardown, null, 2));
        fs.writeFileSync(path.join(evidenceRoot, 'redaction.json'), JSON.stringify({ complete: redacted,
            scope: 'coordinator-generated-credentials-and-mail-body' }));
        writeReports(evidenceRoot, selector, cases);
    }
    if (primaryFailure || !cleanup.complete || cases.some((entry) => !entry.pass)) throw new Error('services proof failed');
}

module.exports = { DependencyCoordinator, loadConfiguration, poll, runCommand, runSuite, caseDiagnostic };
if (require.main === module) {
    runSuite(process.env.CHAT_SERVICE_EVIDENCE_ROOT).catch(() => {
        process.stderr.write('services proof failed; inspect bounded service evidence\n');
        process.exitCode = 1;
    });
}
