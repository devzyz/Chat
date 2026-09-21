'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');

function command(file, args, input = '', timeoutMs = 10000, env = process.env) {
    return new Promise((resolve, reject) => {
        const child = spawn(file, args, { env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        let output = '';
        let error = '';
        const timer = setTimeout(() => child.kill(), timeoutMs);
        child.stdout.on('data', chunk => { output += chunk; });
        child.stderr.on('data', chunk => { error += chunk; });
        child.stdin.on('error', () => {}); // Child exit is reported below, never a successful SQL result.
        child.on('error', failure => { clearTimeout(timer); reject(failure); });
        child.on('close', code => {
            clearTimeout(timer);
            if (code !== 0) reject(new Error(`mysql command failed (${error.match(/ERROR \d+/)?.[0] || code})`));
            else resolve(output.trim());
        });
        child.stdin.end(input);
    });
}

async function startLocalMysql(binDirectory) {
    if (!path.isAbsolute(binDirectory)) throw new Error('absolute MySQL bin directory required');
    const root = await fs.mkdtemp(path.join(os.tmpdir(), 'chat-schema-'));
    const mysqld = path.join(binDirectory, process.platform === 'win32' ? 'mysqld.exe' : 'mysqld');
    const mysql = path.join(binDirectory, process.platform === 'win32' ? 'mysql.exe' : 'mysql');
    const datadir = path.join(root, 'data');
    const basedir = path.dirname(binDirectory);
    let server;
    let closed;
    let execute;
    const close = async () => {
        if (server && server.exitCode === null && server.signalCode === null) {
            if (execute) {
                try { await execute('SHUTDOWN;'); } catch (_) { server.kill(); }
            } else server.kill(); // Only our ChildProcess, never a service/PID-name lookup.
            await Promise.race([closed, new Promise((_, reject) => {
                const timer = setTimeout(() => reject(new Error('owned mysqld teardown timeout')), 15000);
                timer.unref();
            })]);
        }
        const resolved = await fs.realpath(root);
        if (path.dirname(resolved) !== await fs.realpath(os.tmpdir()) ||
            !path.basename(resolved).startsWith('chat-schema-')) throw new Error('unsafe fixture cleanup');
        await fs.rm(resolved, { recursive: true, maxRetries: 10, retryDelay: 100 });
    };
    try {
        const noMonitor = process.platform === 'win32' ? ['--no-monitor'] : [];
        await command(mysqld, ['--no-defaults', ...noMonitor, '--initialize-insecure', `--basedir=${basedir}`,
            `--datadir=${datadir}`, `--log-error=${path.join(root, 'init.log')}`], '', 60000);
        const port = await new Promise((resolve, reject) => {
            const listener = net.createServer();
            listener.on('error', reject);
            listener.listen(0, '127.0.0.1', () => {
                const chosen = listener.address().port;
                listener.close(() => resolve(chosen));
            });
        });
        server = spawn(mysqld, ['--no-defaults', ...noMonitor, `--basedir=${basedir}`, `--datadir=${datadir}`,
            '--bind-address=127.0.0.1', `--port=${port}`, '--mysqlx=0', '--skip-log-bin',
            `--pid-file=${path.join(root, 'mysqld.pid')}`, `--log-error=${path.join(root, 'server.log')}`],
        { windowsHide: true, stdio: 'ignore' });
        closed = new Promise((resolve, reject) => { server.once('exit', resolve); server.once('error', reject); });
        const args = ['--no-defaults', '--protocol=TCP', '--host=127.0.0.1', `--port=${port}`,
            '--user=root', '--connect-timeout=2', '--batch', '--skip-column-names', '--silent'];
        const env = { ...process.env, MYSQL_PWD: '', MYSQL_TEST_LOGIN_FILE: path.join(root, 'no-login-file') };
        execute = sql => command(mysql, args, sql, 10000, env);
        const end = Date.now() + 30000;
        while (true) {
            if (server.exitCode !== null) throw new Error('owned mysqld exited before ready');
            try { if (await execute('SELECT 1;') === '1') break; } catch (error) {
                if (Date.now() >= end) throw error;
            }
            await new Promise(resolve => setTimeout(resolve, 100));
        }
        return { execute, close, mysql, args, root, port, env };
    } catch (error) {
        try { await close(); } catch (cleanup) { error.cleanup = cleanup.message; }
        throw error;
    }
}

module.exports = { startLocalMysql, command };
