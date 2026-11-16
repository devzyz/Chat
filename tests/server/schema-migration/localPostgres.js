'use strict';

const { execFileSync } = require('node:child_process');
const { mkdtempSync, writeFileSync, rmSync, realpathSync, existsSync } = require('node:fs');
const { randomBytes } = require('node:crypto');
const { tmpdir } = require('node:os');
const path = require('node:path');
const net = require('node:net');

// 绑定一个临时回环端口，供本次独占子进程使用。
async function portNumber() {
    const server = net.createServer();
    // 监听回环临时端口并等待操作系统分配端口号。
    await new Promise(
        // 监听成功后才读取由系统分配的端口。
        (resolve, reject) => server.once('error', reject).listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    // 关闭临时监听器后归还端口。
    await new Promise(
        // 释放监听资源后交付端口号。
        resolve => server.close(resolve));
    return port;
}

// 仅管理本次创建的 PG 集群，启动失败时仍尝试清理所属文件。
async function postgresFixture() {
    const bin = process.env.CHAT_PG_BIN;
    if (!bin || !path.isAbsolute(bin)) throw new Error('AbsolutePostgresBinRequired');
    // 定位调用方指定的服务端工具。
    const tools = name => path.join(bin, process.platform === 'win32' ? name + '.exe' : name);
    const root = mkdtempSync(path.join(realpathSync(tmpdir()), 'chat-pg-schema-'));
    const data = path.join(root, 'data');
    const password = randomBytes(32).toString('hex');
    const passFile = path.join(root, 'password');
    const port = await portNumber();
    const env = { ...process.env, CHAT_DB_DRIVER: 'postgresql', CHAT_PG_HOST: '127.0.0.1', CHAT_PG_PORT: String(port),
        CHAT_PG_USER: 'chat_fixture', CHAT_PG_PASSWORD: password, CHAT_PG_DATABASE: 'postgres',
        PGCLIENTENCODING: 'UTF8', PGPASSWORD: password };
    let started = false;
    // 原生命令限定时长，子进程输出不得泄漏凭据。
    function run(file, args, input) {
        return execFileSync(file, args, { env, input, windowsHide: true, encoding: 'utf8', timeout: 45000,
            stdio: ['pipe','pipe','pipe'] }).trim();
    }
    // 先关闭指定数据目录的进程，再清理本次创建的临时目录。
    function close() {
        if (started || existsSync(path.join(data,'postmaster.pid'))) run(tools('pg_ctl'), ['-D',data,'-m','fast','-w','stop']);
        if (path.dirname(realpathSync(root)) !== realpathSync(tmpdir())) throw new Error('UnsafeFixturePath');
        rmSync(root, { recursive: true, force: true });
    }
    try {
        writeFileSync(passFile,password,{ mode: 0o600 });
        run(tools('initdb'),['-D',data,'-E','UTF8','--no-locale','--auth=scram-sha-256','-U',env.CHAT_PG_USER,
            '--pwfile=' + passFile]);
        rmSync(passFile);
        // Windows 后台 postgres 可能继承管道，pg_ctl 启动时只使用日志文件。
        execFileSync(tools('pg_ctl'),['-D',data,'-l',path.join(root,'server.log'),'-w','-o',
            '-h 127.0.0.1 -p ' + port + (process.platform === 'win32' ? '' : ' -k "' + root + '"'),'start'],
            { env, windowsHide: true, stdio: 'ignore', timeout: 45000 });
        started = true;
        // 在隔离库中用独立 psql 会话查询。
        const query = input => run(env.CHAT_PG_CLIENT || tools('psql'), ['-XwqAt','-v','ON_ERROR_STOP=1','-h',
            env.CHAT_PG_HOST,'-p',String(port),'-U',env.CHAT_PG_USER,'-d','postgres','-f','-'], input);
        env.CHAT_PG_CLIENT ||= tools('psql');
        return { env, query, close };
    } catch (error) {
        try { close(); } catch (cleanup) { error.cleanup = cleanup.message; }
        throw error;
    }
}

module.exports = { postgresFixture, portNumber };
