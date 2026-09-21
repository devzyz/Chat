'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const serverRoot = path.resolve(__dirname, '..', '..');
const credentialEnvironment = Object.freeze({
    CHAT_VARIFY_EMAIL_USER: 'test@example.invalid',
    CHAT_VARIFY_EMAIL_PASS: 'test-email-password',
    CHAT_VARIFY_MYSQL_PASSWORD: 'test-mysql-password',
    CHAT_VARIFY_REDIS_PASSWORD: 'test-redis-password'
});

// V07-START-04
test('direct process exits with a failure status when its port is occupied', async (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-startup-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const configPath = path.join(root, 'config.json');
    fs.writeFileSync(configPath, JSON.stringify({
        email: {},
        mysql: { host: '127.0.0.1', port: 3306 },
        redis: { host: '127.0.0.1', port: 1 }
    }));

    const blocker = net.createServer();
    t.after(() => blocker.close());
    await new Promise((resolve, reject) => {
        blocker.once('error', reject);
        blocker.listen(0, '127.0.0.1', resolve);
    });

    const result = spawnSync(process.execPath, ['server.js'], {
        cwd: serverRoot,
        env: {
            ...process.env,
            ...credentialEnvironment,
            CHAT_CONFIG: configPath,
            CHAT_VARIFY_BIND_ADDRESS: `127.0.0.1:${blocker.address().port}`
        },
        encoding: 'utf8',
        timeout: 2000
    });

    assert.equal(result.signal, null, `process did not exit on its own: ${result.error ?? ''}`);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /grpc server failed to start/);
    assert.match(result.stderr, /EADDRINUSE/);
    assert.doesNotMatch(result.stdout, /grpc server started/);
    assert.doesNotMatch(`${result.stdout}\n${result.stderr}`, /test-(?:email|mysql|redis)-password/);
});
