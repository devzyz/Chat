'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');
const grpc = require('@grpc/grpc-js');

const { startServer } = require('../../server');

const serverRoot = path.resolve(__dirname, '..', '..');

// V07-START-01
test('bind failure rejects startup and never starts the server', async () => {
    let starts = 0;
    const server = {
        bindAsync(address, credentials, callback) {
            callback(new Error('address already in use'), 0);
        },
        start() {
            starts += 1;
        }
    };

    await assert.rejects(
        startServer({
            server,
            address: '127.0.0.1:50051',
            credentials: grpc.ServerCredentials.createInsecure(),
            logger: { log() {}, error() {} }
        }),
        /address already in use/
    );
    assert.equal(starts, 0);
});

// V07-START-02
test('zero bound port rejects startup and never starts the server', async () => {
    let starts = 0;
    const server = {
        bindAsync(address, credentials, callback) {
            callback(null, 0);
        },
        start() {
            starts += 1;
        }
    };

    await assert.rejects(
        startServer({
            server,
            address: '127.0.0.1:0',
            credentials: grpc.ServerCredentials.createInsecure(),
            logger: { log() {}, error() {} }
        }),
        /did not bind a port/
    );
    assert.equal(starts, 0);
});

// V07-START-03
test('successful bind starts once and exposes the actual bound port', async () => {
    let starts = 0;
    const server = {
        bindAsync(address, credentials, callback) {
            callback(null, 43210);
        },
        start() {
            starts += 1;
        }
    };

    const port = await startServer({
        server,
        address: '127.0.0.1:0',
        credentials: grpc.ServerCredentials.createInsecure(),
        logger: { log() {}, error() {} }
    });

    assert.equal(port, 43210);
    assert.equal(starts, 1);
});

// V07-START-04
test('direct process exits with a failure status when its port is occupied', async (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-startup-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const configPath = path.join(root, 'config.json');
    fs.writeFileSync(configPath, JSON.stringify({
        email: { user: 'test@example.invalid', pass: 'test-email-password' },
        mysql: { host: '127.0.0.1', port: 3306, passwd: 'test-mysql-password' },
        redis: { host: '127.0.0.1', port: 1, passwd: 'test-redis-password' }
    }));

    const blocker = net.createServer();
    t.after(() => blocker.close());
    await new Promise((resolve, reject) => {
        blocker.once('error', reject);
        blocker.listen(50051, '0.0.0.0', resolve);
    });

    const result = spawnSync(process.execPath, ['server.js'], {
        cwd: serverRoot,
        env: { ...process.env, CHAT_CONFIG: configPath },
        encoding: 'utf8',
        timeout: 2000
    });

    assert.equal(result.signal, null, `process did not exit on its own: ${result.error ?? ''}`);
    assert.notEqual(result.status, 0);
    assert.doesNotMatch(`${result.stdout}\n${result.stderr}`, /test-(?:email|mysql|redis)-password/);
});
