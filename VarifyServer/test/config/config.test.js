'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const serverRoot = path.resolve(__dirname, '..', '..');
const configModule = path.join(serverRoot, 'config.js');
const credentialEnvironment = Object.freeze({
    CHAT_VARIFY_EMAIL_USER: 'sender@example.test',
    CHAT_VARIFY_EMAIL_PASS: 'fixture-email-secret',
    CHAT_VARIFY_REDIS_PASSWORD: 'fixture-redis-secret'
});

// 在临时目录写入带标记的非敏感连接配置并返回文件路径。
function writeConfig(directory, marker) {
    fs.mkdirSync(directory, { recursive: true });
    const file = path.join(directory, 'config.json');
    fs.writeFileSync(file, JSON.stringify({
        email: {},
        redis: { host: `${marker}-redis`, port: 6379 }
    }));
    return file;
}

// 在子进程隔离环境凭据并加载指定配置，返回退出状态和输出。
function loadConfigInChild({ cwd, envConfig, argumentConfig, credentials = credentialEnvironment }) {
    const args = [
        '-e',
        `const config = require(${JSON.stringify(configModule)}); process.stdout.write(JSON.stringify(config));`
    ];
    if (argumentConfig) {
        args.push('--', '--config', argumentConfig);
    }
    const env = { ...process.env };
    delete env.CHAT_VARIFY_MYSQL_PASSWORD;
    for (const name of Object.keys(credentialEnvironment)) {
        delete env[name];
    }
    Object.assign(env, credentials);
    if (envConfig) {
        env.CHAT_CONFIG = envConfig;
    } else {
        delete env.CHAT_CONFIG;
    }
    return spawnSync(process.execPath, args, { cwd, env, encoding: 'utf8' });
}

// 验证命令行配置优先于环境配置且敏感字段取自环境变量。
test('explicit --config takes precedence over CHAT_CONFIG', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 删除本用例创建的临时配置目录。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const environmentConfig = writeConfig(path.join(root, 'environment'), 'environment');
    const argumentConfig = writeConfig(path.join(root, 'argument'), 'argument');

    const result = loadConfigInChild({ cwd: root, envConfig: environmentConfig, argumentConfig });

    assert.equal(result.status, 0, result.stderr);
    const config = JSON.parse(result.stdout);
    assert.equal(config.email_user, credentialEnvironment.CHAT_VARIFY_EMAIL_USER);
    assert.equal(config.email_pass, credentialEnvironment.CHAT_VARIFY_EMAIL_PASS);
    assert.equal(config.redis_passwd, credentialEnvironment.CHAT_VARIFY_REDIS_PASSWORD);
    assert.equal(config.redis_host, 'argument-redis');
});

// 验证环境指定配置优先于工作目录默认文件。
test('CHAT_CONFIG takes precedence over the working-directory default', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 删除本用例创建的临时配置目录。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    writeConfig(root, 'default');
    const environmentConfig = writeConfig(path.join(root, 'environment'), 'environment');

    const result = loadConfigInChild({ cwd: root, envConfig: environmentConfig });

    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(result.stdout).redis_host, 'environment-redis');
});

// 验证默认配置路径及验证码键前缀。
test('config.json is loaded from the working directory by default', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 删除本用例创建的临时配置目录。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    writeConfig(root, 'default');

    const result = loadConfigInChild({ cwd: root });

    assert.equal(result.status, 0, result.stderr);
    const config = JSON.parse(result.stdout);
    assert.equal(config.redis_host, 'default-redis');
    assert.equal(config.code_prefix, 'code_');
});

// 验证损坏的 JSON 配置导致加载失败。
test('malformed configuration exits with a failure status', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 删除本用例创建的临时配置目录。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const malformed = path.join(root, 'broken.json');
    fs.writeFileSync(malformed, '{"broken":');

    const result = loadConfigInChild({ cwd: root, argumentConfig: malformed });

    assert.notEqual(result.status, 0);
});

// 旧部署可以保留非敏感的 MySQL 地址，但验证码服务不需要数据库凭据。
test('legacy MySQL addresses are ignored without a database credential', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 仅回收本次用例创建的配置。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const file = writeConfig(root, 'legacy');
    const settings = JSON.parse(fs.readFileSync(file, 'utf8'));
    settings.mysql = { host: 'unused.invalid', port: 3306 };
    fs.writeFileSync(file, JSON.stringify(settings));

    const result = loadConfigInChild({ cwd: root });
    assert.equal(result.status, 0, result.stderr);
    const loaded = JSON.parse(result.stdout);
    assert.equal(loaded.redis_host, 'legacy-redis');
    for (const field of ['mysql_host', 'mysql_port', 'mysql_passwd']) {
        assert.equal(Object.hasOwn(loaded, field), false);
    }
});

for (const missingName of Object.keys(credentialEnvironment)) {
    // 逐项验证必需凭据缺失时失败且诊断不泄露其他凭据。
    test(`missing ${missingName} is rejected without exposing credentials`, (t) => {
        const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
        // 删除本用例创建的临时配置目录。
        t.after(() => fs.rmSync(root, { recursive: true, force: true }));
        writeConfig(root, 'default');
        const credentials = { ...credentialEnvironment };
        delete credentials[missingName];

        const result = loadConfigInChild({ cwd: root, credentials });

        assert.notEqual(result.status, 0);
        assert.match(result.stderr, new RegExp(missingName));
        for (const value of Object.values(credentialEnvironment)) {
            assert.equal(result.stderr.includes(value), false);
        }
    });
}

// 验证旧配置文件中的明文密码被拒绝且不出现在诊断中。
test('plaintext credential fields in config.json are rejected', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 删除本用例创建的临时配置目录。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const configPath = writeConfig(root, 'plaintext');
    const baseline = JSON.parse(fs.readFileSync(configPath, 'utf8'));
    const legacySecret = 'legacy-fixture-secret';
    const forbidden = {
        email: { user: 'legacy@example.test', pass: legacySecret },
        mysql: { host: 'mysql', port: 3306, passwd: legacySecret },
        redis: { host: 'redis', port: 6379, passwd: legacySecret }
    };
    for (const [section, fields] of Object.entries(forbidden)) {
        fs.writeFileSync(configPath, JSON.stringify({ ...baseline, [section]: fields }));
        const result = loadConfigInChild({ cwd: root });
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /environment variables/);
        assert.equal(result.stderr.includes(legacySecret), false);
    }
});

// 验证显式无认证 SMTP 配置可不提供邮件密码。
test('SMTP no-auth configuration needs no SMTP password and retains sender identity', (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    // 删除本用例创建的临时配置目录。
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const file = writeConfig(root, 'smtp');
    const config = JSON.parse(fs.readFileSync(file, 'utf8'));
    config.email = { host: '127.0.0.1', port: 1025, secure: false, auth: 'none', deadlineMs: 1500 };
    fs.writeFileSync(file, JSON.stringify(config));
    const credentials = { ...credentialEnvironment };
    delete credentials.CHAT_VARIFY_EMAIL_PASS;
    const result = loadConfigInChild({ cwd: root, credentials });
    assert.equal(result.status, 0, result.stderr);
    const loaded = JSON.parse(result.stdout);
    assert.deepEqual(loaded.smtp, config.email);
    assert.equal(loaded.email_user, credentials.CHAT_VARIFY_EMAIL_USER);
    assert.equal(loaded.email_pass, undefined);
});
