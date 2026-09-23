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
    CHAT_VARIFY_MYSQL_PASSWORD: 'fixture-mysql-secret',
    CHAT_VARIFY_REDIS_PASSWORD: 'fixture-redis-secret'
});

/** 在临时目录写入带标记的非敏感连接配置并返回文件路径。 */ function writeConfig(directory, marker) {
    fs.mkdirSync(directory, { recursive: true });
    const file = path.join(directory, 'config.json');
    fs.writeFileSync(file, JSON.stringify({
        email: {},
        mysql: { host: `${marker}-mysql`, port: 3306 },
        redis: { host: `${marker}-redis`, port: 6379 }
    }));
    return file;
}

/** 在子进程隔离环境凭据并加载指定配置，返回退出状态和输出。 */ function loadConfigInChild({ cwd, envConfig, argumentConfig, credentials = credentialEnvironment }) {
    const args = [
        '-e',
        `const config = require(${JSON.stringify(configModule)}); process.stdout.write(JSON.stringify(config));`
    ];
    if (argumentConfig) {
        args.push('--', '--config', argumentConfig);
    }
    const env = { ...process.env };
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

test('explicit --config takes precedence over CHAT_CONFIG', /** 验证命令行配置优先于环境配置且敏感字段取自环境变量。 */ (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
    const environmentConfig = writeConfig(path.join(root, 'environment'), 'environment');
    const argumentConfig = writeConfig(path.join(root, 'argument'), 'argument');

    const result = loadConfigInChild({ cwd: root, envConfig: environmentConfig, argumentConfig });

    assert.equal(result.status, 0, result.stderr);
    const config = JSON.parse(result.stdout);
    assert.equal(config.email_user, credentialEnvironment.CHAT_VARIFY_EMAIL_USER);
    assert.equal(config.email_pass, credentialEnvironment.CHAT_VARIFY_EMAIL_PASS);
    assert.equal(config.mysql_passwd, credentialEnvironment.CHAT_VARIFY_MYSQL_PASSWORD);
    assert.equal(config.redis_passwd, credentialEnvironment.CHAT_VARIFY_REDIS_PASSWORD);
    assert.equal(config.mysql_host, 'argument-mysql');
    assert.equal(config.redis_host, 'argument-redis');
});

test('CHAT_CONFIG takes precedence over the working-directory default', /** 验证环境指定配置优先于工作目录默认文件。 */ (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
    writeConfig(root, 'default');
    const environmentConfig = writeConfig(path.join(root, 'environment'), 'environment');

    const result = loadConfigInChild({ cwd: root, envConfig: environmentConfig });

    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(result.stdout).mysql_host, 'environment-mysql');
});

test('config.json is loaded from the working directory by default', /** 验证默认配置路径及验证码键前缀。 */ (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
    writeConfig(root, 'default');

    const result = loadConfigInChild({ cwd: root });

    assert.equal(result.status, 0, result.stderr);
    const config = JSON.parse(result.stdout);
    assert.equal(config.mysql_host, 'default-mysql');
    assert.equal(config.code_prefix, 'code_');
});

test('malformed configuration exits with a failure status', /** 验证损坏的 JSON 配置导致加载失败。 */ (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
    const malformed = path.join(root, 'broken.json');
    fs.writeFileSync(malformed, '{"broken":');

    const result = loadConfigInChild({ cwd: root, argumentConfig: malformed });

    assert.notEqual(result.status, 0);
});

for (const missingName of Object.keys(credentialEnvironment)) {
    test(`missing ${missingName} is rejected without exposing credentials`, /** 逐项验证必需凭据缺失时失败且诊断不泄露其他凭据。 */ (t) => {
        const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
        t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
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

test('plaintext credential fields in config.json are rejected', /** 验证旧配置文件中的明文密码被拒绝且不出现在诊断中。 */ (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
    const configPath = path.join(root, 'config.json');
    const legacySecret = 'legacy-fixture-secret';
    fs.writeFileSync(configPath, JSON.stringify({
        email: { user: 'legacy@example.test', pass: legacySecret },
        mysql: { host: 'mysql', port: 3306, passwd: legacySecret },
        redis: { host: 'redis', port: 6379, passwd: legacySecret }
    }));

    const result = loadConfigInChild({ cwd: root });

    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /environment variables/);
    assert.equal(result.stderr.includes(legacySecret), false);
});

test('SMTP no-auth configuration needs no SMTP password and retains sender identity', /** 验证显式无认证 SMTP 配置可不提供邮件密码。 */ (t) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'varify-config-'));
    t.after(/** 删除本用例创建的临时配置目录。 */ () => fs.rmSync(root, { recursive: true, force: true }));
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
