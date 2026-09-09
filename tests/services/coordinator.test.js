'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { DependencyCoordinator, loadConfiguration, poll, runCommand } = require('./dependencyCoordinator');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const lock = require('./services.lock.json');
const { reportGroups, writeReports } = require('./serviceReports');
const { verifyDependencies } = require('./serviceRuntime');

test('service artifacts require bundled hiredis and reject build-tree or missing dependencies', () => {
    const root = '/tmp/service-launcher';
    const system = 'libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x1)\n';
    const bundled = `libhiredis.so.1 => ${root}/libhiredis.so.1 (0x2)`;
    assert.doesNotThrow(() => verifyDependencies(system, root));
    assert.doesNotThrow(() => verifyDependencies(system + bundled, root, true));
    assert.throws(() => verifyDependencies(system + bundled, root));
    for (const wrong of [
        'libhiredis.so.1 => /work/.ci/vcpkg_installed/lib/libhiredis.so.1 (0x2)',
        'libhiredis.so.1 => /tmp/service-launcher-other/libhiredis.so.1 (0x2)',
        'libhiredis.so.1 => not found',
        'libunexpected.so.1 => /lib/x86_64-linux-gnu/libunexpected.so.1 (0x2)',
        'libhiredis.so.1 => /lib/x86_64-linux-gnu/libhiredis.so.1 (0x2)'
    ]) assert.throws(() => verifyDependencies(system + wrong, root, true));
    assert.throws(() => verifyDependencies(system, root, true));
});

test('adapter reports are separate and missing selected cases fail closed', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-reports-'));
    try {
        const cases = Array.from({ length: 12 }, (_, index) => ({
            id: `T10-SVC-${String(index + 1).padStart(2, '0')}`, name: 'bounded contract', pass: true, seconds: 0
        }));
        writeReports(root, '3C-06', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_services.xml'), 'utf8'), /tests="12" failures="0"/);
        assert.match(fs.readFileSync(path.join(root, 'varify_smtp.xml'), 'utf8'), /<failure /);
        assert.equal(reportGroups('3C-adapters').length, 4);
        assert.throws(() => reportGroups('unexpected'), /selector/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('outer cleanup cannot pass when a required adapter report is absent or failed', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-reports-'));
    try {
        fs.writeFileSync(path.join(root, 'teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'process-teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'service-endpoints.json'), '{}');
        fs.writeFileSync(path.join(root, 'linux_services.xml'), '<testsuite tests="12" failures="0"/>');
        await assert.rejects(runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false', CHAT_SERVICE_SELECTOR: '3C-06' }
        }), /command failed/);
        assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'teardown.json'), 'utf8')).complete, false);
        fs.writeFileSync(path.join(root, 'teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'linux_services.xml'), '<testsuite tests="12" failures="0"/>');
        fs.writeFileSync(path.join(root, 'varify_smtp.xml'), '<testsuite tests="1" failures="1"><testcase><failure/></testcase></testsuite>');
        await assert.rejects(runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false', CHAT_SERVICE_SELECTOR: '3C-06' }
        }), /command failed/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

function lifecycleFixture() {
    const config = { host: '127.0.0.1', ids: { mailpit: 'c'.repeat(64), redis: 'a'.repeat(64) },
        ports: { redis: 32100, mysql: 32101, smtp: 32102, mailpit: 32103 },
        prefix: 'owned:', recipient: 'owned@example.invalid' };
    const containers = {};
    for (const service of ['mailpit', 'redis']) {
        containers[service] = { Id: config.ids[service], Config: { Image: lock.services[service].image },
            State: { Running: true }, NetworkSettings: { Ports: Object.fromEntries(
                lock.services[service].ports.map((port) => [`${port}/tcp`, [{ HostIp: '127.0.0.1',
                    HostPort: String(config.ports[port === 1025 ? 'smtp' : service]) }]])) } };
    }
    const coordinator = new DependencyCoordinator(config, { Redis: class {}, nodemailer: {},
        runCommand: async (command, args) => {
            assert.equal(command, 'docker');
            const service = Object.keys(config.ids).find((name) => config.ids[name] === args.at(-1));
            assert.ok(service, 'only exact owned ID may be addressed');
            if (args[0] === 'inspect') return JSON.stringify([containers[service]]);
            assert.ok(['start', 'stop'].includes(args[0]));
            containers[service].State.Running = args[0] === 'start';
            if (args[0] === 'start') {
                for (const bindings of Object.values(containers[service].NetworkSettings.Ports)) {
                    bindings[0].HostPort = String(Number(bindings[0].HostPort) + 100);
                }
            }
            return '';
        } });
    return { coordinator, config, containers };
}

test('restart refreshes both Mailpit ports and Redis port in the shared configuration', async () => {
    const { coordinator, config } = lifecycleFixture();
    const ports = config.ports;
    for (const service of ['mailpit', 'redis']) {
        await coordinator.inspect(service);
        await coordinator.lifecycle(service, 'stop');
        await coordinator.lifecycle(service, 'start');
    }
    assert.equal(coordinator.config, config);
    assert.equal(config.ports, ports);
    assert.deepEqual(ports, { redis: 32200, mysql: 32101, smtp: 32202, mailpit: 32203 });
    let smtpPort;
    coordinator.nodemailer = { createTransport: (options) => { smtpPort = options.port; return {}; } };
    coordinator.smtp();
    assert.equal(smtpPort, ports.smtp);
});

test('initial ownership still requires the workflow port and rejects unverified restart', async () => {
    const { coordinator, config, containers } = lifecycleFixture();
    containers.mailpit.NetworkSettings.Ports['8025/tcp'][0].HostPort = '32203';
    await assert.rejects(coordinator.inspect('mailpit'));
    assert.equal(coordinator.owned.has('mailpit'), false);
    await assert.rejects(coordinator.lifecycle('mailpit', 'start'), /unverified/);
    await assert.rejects(coordinator.inspect('mailpit', true), /unverified/);
    assert.equal(config.ports.mailpit, 32103);
});

test('restart rejects invalid identity and bindings without publishing partial ports', async () => {
    const corruptions = [
        (container) => { container.Id = 'd'.repeat(64); },
        (container) => { container.Config.Image = 'unowned-image'; },
        (container) => { container.State.Running = false; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'] = []; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostIp = '0.0.0.0'; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '65536'; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '0'; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '32203junk'; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '32100'; },
        (container) => { container.NetworkSettings.Ports['8025/tcp'].push({ HostIp: '127.0.0.1', HostPort: '32204' }); }
    ];
    for (const corrupt of corruptions) {
        const { coordinator, config } = lifecycleFixture();
        await coordinator.inspect('mailpit');
        await coordinator.lifecycle('mailpit', 'stop');
        const before = { ...config.ports };
        const command = coordinator.runCommand;
        coordinator.runCommand = async (...args) => {
            const result = await command(...args);
            if (args[1][0] !== 'inspect') return result;
            const [container] = JSON.parse(result);
            corrupt(container);
            return JSON.stringify([container]);
        };
        await assert.rejects(coordinator.lifecycle('mailpit', 'start'), (error) => {
            assert.deepEqual(error.diagnostic, { stage: 'mailpit-restart-endpoints', category: 'ERR_ASSERTION' });
            return true;
        });
        assert.deepEqual(config.ports, before);
    }
});

test('refreshed Mailpit endpoint drives real HTTP health and cleanup requests', { timeout: 5000 }, async (t) => {
    const requests = [];
    let messages = [{ ID: 'owned-message' }];
    const server = require('node:http').createServer((request, response) => {
        requests.push({ method: request.method, url: request.url });
        if (request.url === '/readyz') { response.end('ready'); return; }
        if (request.method === 'DELETE') { messages = []; response.end(); return; }
        response.setHeader('Content-Type', 'application/json');
        response.end(JSON.stringify({ messages }));
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(() => new Promise((resolve) => { server.closeAllConnections(); server.close(resolve); }));
    const { coordinator, config, containers } = lifecycleFixture();
    const oldPorts = config.ports;
    await coordinator.inspect('mailpit');
    await coordinator.lifecycle('mailpit', 'stop');
    const command = coordinator.runCommand;
    coordinator.runCommand = async (...args) => {
        const result = await command(...args);
        if (args[1][0] === 'start') {
            containers.mailpit.NetworkSettings.Ports['8025/tcp'][0].HostPort = String(server.address().port);
        }
        return result;
    };
    await coordinator.lifecycle('mailpit', 'start');
    assert.equal(config.ports, oldPorts);
    assert.equal(config.ports.mailpit, server.address().port);
    await poll(() => coordinator.mailApi('/readyz'), 1000);
    assert.deepEqual(await coordinator.clearData(), []);
    assert.equal(requests[0].url, '/readyz');
    assert.ok(requests.some(({ method, url }) => method === 'DELETE' && url === '/api/v1/messages'));
    assert.equal(messages.length, 0);
});

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
