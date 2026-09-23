'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');

/** 运行有期限的所属命令，成功返回输出，失败只公开数字 SQL 错误或退出码。 */
function command(file, args, input = '', timeoutMs = 10000, env = process.env) {
    return new Promise(/** 启动命令并安装输出收集、期限和退出处理。 */ (resolve, reject) => {
        const child = spawn(file, args, { env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        let output = '';
        let error = '';
        const timer = setTimeout(/** 达到命令期限时终止所属子进程。 */ () => child.kill(), timeoutMs);
        child.stdout.on('data', /** 累计命令标准输出供成功结果读取。 */ chunk => { output += chunk; });
        child.stderr.on('data', /** 累计标准错误供安全错误码提取。 */ chunk => { error += chunk; });
        child.stdin.on('error', /** 消费输入管道错误，最终以子进程退出结果判定失败。 */ () => {}); // Child exit is reported below, never a successful SQL result.
        child.on('error', /** 启动失败时清理计时器并拒绝等待。 */ failure => { clearTimeout(timer); reject(failure); });
        child.on('close', /** 清理计时器并按退出码返回输出或安全失败原因。 */ code => {
            clearTimeout(timer);
            if (code !== 0) reject(new Error(`mysql command failed (${error.match(/ERROR \d+/)?.[0] || code})`));
            else resolve(output.trim());
        });
        child.stdin.end(input);
    });
}

/** 使用明确 MySQL 工具路径创建隔离临时数据目录及随机回环实例，不控制个人服务。 */
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
    const close = /** 请求所属 MySQL 正常退出，有限等待并核验临时路径后清理。 */ async () => {
        if (server && server.exitCode === null && server.signalCode === null) {
            if (execute) {
                try { await execute('SHUTDOWN;'); } catch (_) { server.kill(); }
            } else server.kill(); // Only our ChildProcess, never a service/PID-name lookup.
            await Promise.race([closed, new Promise(/** 为所属 MySQL 停机设置有限等待。 */ (_, reject) => {
                const timer = setTimeout(/** 停机超时则拒绝清理等待。 */ () => reject(new Error('owned mysqld teardown timeout')), 15000);
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
        const port = await new Promise(/** 通过临时监听器选择可用回环端口。 */ (resolve, reject) => {
            const listener = net.createServer();
            listener.on('error', reject);
            listener.listen(0, '127.0.0.1', /** 取得实际端口后关闭临时监听器。 */ () => {
                const chosen = listener.address().port;
                listener.close(/** 监听器关闭后返回所选端口。 */ () => resolve(chosen));
            });
        });
        server = spawn(mysqld, ['--no-defaults', ...noMonitor, `--basedir=${basedir}`, `--datadir=${datadir}`,
            '--bind-address=127.0.0.1', `--port=${port}`, '--mysqlx=0', '--skip-log-bin',
            `--pid-file=${path.join(root, 'mysqld.pid')}`, `--log-error=${path.join(root, 'server.log')}`],
        { windowsHide: true, stdio: 'ignore' });
        closed = new Promise(/** 把所属 MySQL 进程的退出或启动失败转换为可等待结果。 */ (resolve, reject) => { server.once('exit', resolve); server.once('error', reject); });
        const args = ['--no-defaults', '--protocol=TCP', '--host=127.0.0.1', `--port=${port}`,
            '--user=root', '--connect-timeout=2', '--batch', '--skip-column-names', '--silent'];
        const env = { ...process.env, MYSQL_PWD: '', MYSQL_TEST_LOGIN_FILE: path.join(root, 'no-login-file') };
        execute = /** 在固定测试环境和期限内执行给定 SQL。 */ sql => command(mysql, args, sql, 10000, env);
        const end = Date.now() + 30000;
        while (true) {
            if (server.exitCode !== null) throw new Error('owned mysqld exited before ready');
            try { if (await execute('SELECT 1;') === '1') break; } catch (error) {
                if (Date.now() >= end) throw error;
            }
            await new Promise(/** 在启动就绪探测之间等待短间隔。 */ resolve => setTimeout(resolve, 100));
        }
        return { execute, close, mysql, args, root, port, env };
    } catch (error) {
        try { await close(); } catch (cleanup) { error.cleanup = cleanup.message; }
        throw error;
    }
}

module.exports = { startLocalMysql, command };
