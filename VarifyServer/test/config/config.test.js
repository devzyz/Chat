'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const serverRoot = path.resolve(__dirname, '..', '..');
const configModule = path.join(serverRoot, 'config.js');

function writeConfig(directory, marker) {
    fs.mkdirSync(directory, { recursive: true });
    const file = path.join(directory, 'config.json');
    fs.writeFileSync(file, JSON.stringify({
        email: { user: `${marker}@example.com`, pass: `${marker}-email-secret` },
        mysql: { host: `${marker}-mysql`, port: 3306, passwd: `${marker}-mysql-secret` },
        redis: { host: `${marker}-redis`, port: 6379, passwd: `${marker}-redis-secret` }
    }));
    return file;
}

function loadConfigInChild({ cwd, envConfig, argumentConfig }) {
    const args = [
        '-e',
        `const config = require(${JSON.stringify(configModule)}); process.stdout.write(JSON.stringify(config));`
    ];
    if (argumentConfig) {
        args.push('--', '--config', argumentConfig);
    }
    const env = { ...process.env };
    if (envConfig) {
        env.CHAT_CONFIG = envConfig;
    } else {
        delete env.CHAT_CONFIG;
    }
    return spawnSync(process.execPath, args, { cwd, env, encoding: 'utf8' });
}

test('explicit --config takes precedence over CHAT_CONFIG', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const environmentConfig = writeConfig(path.join(root, 'environment'), 'environment');
    const argumentConfig = writeConfig(path.join(root, 'argument'), 'argument');

    const result = loadConfigInChild({ cwd: root, envConfig: environmentConfig, argumentConfig });

    assert.equal(result.status, 0, result.stderr);
    const config = JSON.parse(result.stdout);
    assert.equal(config.email_user, 'argument@example.com');
    assert.equal(config.mysql_host, 'argument-mysql');
    assert.equal(config.redis_host, 'argument-redis');
});

test('CHAT_CONFIG takes precedence over the working-directory default', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    writeConfig(root, 'default');
    const environmentConfig = writeConfig(path.join(root, 'environment'), 'environment');

    const result = loadConfigInChild({ cwd: root, envConfig: environmentConfig });

    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(result.stdout).email_user, 'environment@example.com');
});

test('config.json is loaded from the working directory by default', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    writeConfig(root, 'default');

    const result = loadConfigInChild({ cwd: root });

    assert.equal(result.status, 0, result.stderr);
    const config = JSON.parse(result.stdout);
    assert.equal(config.email_user, 'default@example.com');
    assert.equal(config.code_prefix, 'code_');
});

test('malformed configuration exits with a failure status', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const malformed = path.join(root, 'broken.json');
    fs.writeFileSync(malformed, '{"broken":');

    const result = loadConfigInChild({ cwd: root, argumentConfig: malformed });

    assert.notEqual(result.status, 0);
});
