'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { DependencyCoordinator, loadConfiguration, poll, runCommand } = require('./dependencyCoordinator');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const lock = require('./services.lock.json');

test('bootstrap rotates both root accounts without switching authenticated account', { timeout: 5000 }, async (t) => {
    const server = net.createServer((socket) => socket.end(Buffer.from([1, 0, 0, 0, 10])));
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(() => new Promise((resolve) => server.close(resolve)));
    const passwords = { localhost: '', '%': '' };
    const calls = [];
    const coordinator = new DependencyCoordinator({ ids: { mysql: 'b'.repeat(64) },
        host: '127.0.0.1', ports: { mysql: server.address().port } }, {
        Redis: class {}, nodemailer: {},
        runCommand: async (executable, args, options) => {
            const host = args.includes('--protocol=SOCKET') ? 'localhost' : '%';
            assert.ok(options.env.MYSQL_PWD === passwords[host], 'selected root account rejects password');
            assert.equal(executable, 'docker');
            assert.ok(!args.some((arg) => arg.includes(coordinator.password)));
            calls.push(options.input);
            if (options.input === 'SELECT 1;') return '1';
            if (options.input === 'SELECT CURRENT_USER();') return 'root@localhost';
            if (options.input.includes('SELECT Host')) return 'localhost\n%';
            const change = options.input.match(/^ALTER USER 'root'@'(localhost|%)' IDENTIFIED BY '([a-f0-9]+)';$/);
            assert.ok(change, 'only known bootstrap statements');
            passwords[change[1]] = change[2];
            return '';
        }
    });
    coordinator.owned.add('mysql');
    coordinator.redis = async () => ({ ping: async () => 'PONG', config: async () => 'OK', disconnect() {} });
    coordinator.mailApi = async () => true;
    await coordinator.bootstrap();
    assert.equal(await coordinator.sql('SELECT 1;'), '1');
    assert.equal(passwords.localhost, coordinator.password);
    assert.equal(passwords['%'], coordinator.password);
    assert.ok(calls.some((sql) => sql.includes("ALTER USER 'root'@'%'")));
});

test('bootstrap failures expose only fixed stage and safe error category', { timeout: 5000 }, async (t) => {
    const server = net.createServer((socket) => socket.end(Buffer.from([1, 0, 0, 0, 10])));
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(() => new Promise((resolve) => server.close(resolve)));
    const secret = require('node:crypto').randomBytes(24).toString('hex');
    const coordinator = new DependencyCoordinator({ ids: { mysql: 'b'.repeat(64) },
        host: '127.0.0.1', ports: { mysql: server.address().port } }, {
        Redis: class {}, nodemailer: {},
        runCommand: async (executable, args, options) => {
            if (options.input === 'SELECT 1;') return '1';
            throw Object.assign(new Error(secret), { code: secret, diagnostic: { stage: secret } });
        }
    });
    coordinator.owned.add('mysql');
    await assert.rejects(coordinator.bootstrap(), (error) => {
        assert.deepEqual(error.diagnostic, { stage: 'mysql-account', category: 'operation-failed' });
        assert.ok(!JSON.stringify(error).includes(secret));
        assert.ok(!error.message.includes(secret));
        return true;
    });
});

test('service lock matches hosted workflow images and dynamically mapped ports', () => {
    const workflow = fs.readFileSync(path.resolve(__dirname, '../../.github/workflows/linux-ci.yml'), 'utf8');
    assert.equal(lock.schemaVersion, 1);
    assert.ok(lock.healthDeadlineMs > 0 && lock.healthDeadlineMs <= 120000);
    assert.ok(lock.commandDeadlineMs > 0 && lock.commandDeadlineMs <= 5000);
    assert.equal(lock.gracefulStopSeconds, 10);
    const cmake = fs.readFileSync(path.resolve(__dirname, '../../CMakeLists.txt'), 'utf8');
    assert.ok(cmake.includes('set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")'));
    assert.ok(workflow.includes('cp out/build/linux-x64-release/bin/service_run out/phase3c/service-launcher/'));
    const serviceJob = workflow.split('  disposable-services:')[1].split('  downstream-contract:')[0];
    const jobConfiguration = serviceJob.split('    steps:')[0];
    assert.ok(!jobConfiguration.includes('${{ job.'), 'job context is only available in step env');
    for (const name of ['Prove bounded disposable service lifecycle', 'Preserve fail-closed outer evidence']) {
        const step = serviceJob.split(`      - name: ${name}`)[1].split('      - name:')[0];
        assert.ok(step.includes('        env:'), `${name} must inject its own runtime service identity`);
        for (const service of ['redis', 'mysql', 'mailpit']) {
            assert.ok(step.includes(`CHAT_${service.toUpperCase()}_CONTAINER: \${{ job.services.${service}.id }}`));
        }
    }
    for (const service of Object.values(lock.services)) {
        assert.match(service.image, /:[a-z0-9.]+@sha256:[a-f0-9]{64}$/);
        assert.ok(workflow.includes(`image: ${service.image}`));
        for (const port of service.ports) assert.ok(workflow.includes(`127.0.0.1::${port}`));
        assert.ok(service.health && service.source.startsWith('https://'));
    }
});

test('configuration rejects personal hosts, invalid ports and unowned container IDs', () => {
    assert.throws(() => loadConfiguration({}), /configuration/);
    const env = {
        CHAT_SERVICE_RUN_ID: 'a'.repeat(32), CHAT_SERVICE_HOST: '127.0.0.1',
        CHAT_REDIS_PORT: '32100', CHAT_MYSQL_PORT: '32101', CHAT_SMTP_PORT: '32102', CHAT_MAILPIT_PORT: '32103',
        CHAT_REDIS_CONTAINER: 'a'.repeat(64), CHAT_MYSQL_CONTAINER: 'b'.repeat(64),
        CHAT_MAILPIT_CONTAINER: 'c'.repeat(64)
    };
    assert.equal(loadConfiguration(env).database, `chat_${'a'.repeat(32)}`);
    assert.throws(() => loadConfiguration({ ...env, CHAT_SERVICE_HOST: '192.168.1.1' }), /configuration/);
    assert.throws(() => loadConfiguration({ ...env, CHAT_MYSQL_PORT: '3306junk' }), /configuration/);
    assert.throws(() => loadConfiguration({ ...env, CHAT_MYSQL_CONTAINER: 'shared-mysql' }), /configuration/);
});

test('failed health checks consume a finite deadline and never become PASS', async () => {
    await assert.rejects(poll(async () => false, 30), /health deadline/);
});

test('subprocess failure and timeout discard potentially sensitive command output', async () => {
    await assert.rejects(runCommand(process.execPath, ['-e', 'process.stderr.write("secret");process.exit(2)']),
        (error) => error.message === 'command failed (2)');
    await assert.rejects(runCommand(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], { timeout: 50 }),
        /command deadline/);
});

test('missing outer evidence remains failed and emits all required diagnostics', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-evidence-'));
    try {
        await assert.rejects(runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false' }
        }), /command failed/);
        assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'teardown.json'))).complete, false);
        assert.ok(fs.existsSync(path.join(root, 'service-endpoints.json')));
        assert.match(fs.readFileSync(path.join(root, 'linux_services.xml'), 'utf8'), /failures="1"/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('complete inner and process cleanup preserves real service report', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-evidence-'));
    try {
        fs.writeFileSync(path.join(root, 'teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'process-teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'service-endpoints.json'), '{}');
        const report = '<testsuite name="actual" tests="12" failures="0"/>';
        fs.writeFileSync(path.join(root, 'linux_services.xml'), report);
        await runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false' }
        });
        assert.equal(fs.readFileSync(path.join(root, 'linux_services.xml'), 'utf8'), report);
    } finally { fs.rmSync(root, { recursive: true }); }
});
