'use strict';

const { spawn } = require('node:child_process');
const { randomBytes } = require('node:crypto');
const os = require('node:os');
const path = require('node:path');

// One mysql client process is one server session: advisory locks survive DDL commits.
// --force is safe here because execute accepts ONE statement, then a marker SELECT.
// SQL diagnostics are reduced to numeric codes; raw SQL/credentials never escape.
/** 用单一 mysql 子进程保留跨 DDL 的会话锁；串行执行语句，输出只暴露结果或数字错误码。 */ class MysqlSession {
    /** 启动隔离登录配置的客户端并安装有界输出、退出和流错误处理。 */ constructor(file, args, env = process.env, timeoutMs = 10000) {
        this.timeoutMs = timeoutMs;
        // --no-defaults alone still reads .mylogin.cnf on MySQL 8.0. Pin a fresh
        // nonexistent login file instead of consuming personal saved credentials.
        const childEnv = { ...env, MYSQL_TEST_LOGIN_FILE:
            path.join(os.tmpdir(), `chat-no-login-${randomBytes(16).toString('hex')}`) };
        this.child = spawn(file, args, { env: childEnv, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        this.pending = null;
        this.output = '';
        this.errors = '';
        this.exited = false;
        this.closed = new Promise(/** 保存关闭完成通知，供调用方等待子进程真正退出。 */ resolve => { this.resolveClosed = resolve; });
        this.child.stdout.on('data', /** 合并标准输出并尝试消费完整的私有结束标记。 */ chunk => { this.output += chunk.toString().replace(/\r/g, ''); this.consume(); });
        this.child.stderr.on('data', /** 累计诊断并在超出上限时终止客户端。 */ chunk => {
            this.errors += chunk;
            if (this.errors.length > 65536) this.abort('MysqlOutputLimit');
        });
        this.child.stdin.on('error', /** 输入管道出错时拒绝当前语句并终止客户端。 */ () => this.abort('MysqlUnavailable'));
        this.child.on('error', /** 子进程启动失败时统一报告客户端不可用。 */ () => this.abort('MysqlUnavailable'));
        this.child.on('close', /** 子进程关闭后拒绝未完成语句并通知关闭等待者。 */ () => {
            this.exited = true;
            this.fail('MysqlUnavailable');
            this.resolveClosed();
        });
    }

    /** 以指定安全错误码拒绝当前语句并清除期限计时器。 */ fail(code) {
        if (!this.pending) return;
        const pending = this.pending;
        this.pending = null;
        clearTimeout(pending.timer);
        pending.reject(new Error(code));
    }

    /** 拒绝当前语句并终止仍在运行的客户端。 */ abort(code) {
        this.fail(code);
        if (!this.exited) this.child.kill();
    }

    /** 等待完整结束标记后结算当前语句；超限中止，SQL 错误仅返回数字代码。 */ consume() {
        if (this.output.length > 4 * 1024 * 1024) return this.abort('MysqlOutputLimit');
        const pending = this.pending;
        if (!pending) return;
        const match = this.output.match(new RegExp(`${pending.marker}:(\\d+)\\n`));
        if (!match) return;
        const value = this.output.slice(0, match.index).trim();
        this.output = this.output.slice(match.index + match[0].length);
        this.errors = '';
        this.pending = null;
        clearTimeout(pending.timer);
        if (match[1] !== '0') pending.reject(new Error(`MysqlError:${match[1]}`));
        else pending.resolve(value);
    }

    /** 提交一条 SQL 并返回结果 Promise；不允许并发语句，超时终止整个会话。 */ execute(sql) {
        if (this.exited || this.pending) return Promise.reject(new Error('MysqlSessionUnavailable'));
        const marker = `chat_end_${randomBytes(16).toString('hex')}`;
        return new Promise(/** 登记本次语句的结束标记和期限，再写入 SQL 与诊断查询。 */ (resolve, reject) => {
            const timer = setTimeout(/** 语句超过期限时终止客户端并拒绝等待结果。 */ () => this.abort('MysqlDeadlineExceeded'), this.timeoutMs);
            this.pending = { marker, resolve, reject, timer };
            // A private delimiter lets CREATE PROCEDURE contain internal semicolons.
            const delimiter = `d${randomBytes(6).toString('hex')}`;
            // Get diagnostics in SQL before the marker, avoiding stdout/stderr races.
            // An absent condition leaves initialized values unchanged (warning 1758).
            const request = `SET @chat_errno=0,@chat_state='00000';\n` +
                `delimiter ${delimiter}\n${sql}\n${delimiter}\ndelimiter ;\n` +
                'GET DIAGNOSTICS CONDITION 1 @chat_errno=MYSQL_ERRNO,@chat_state=RETURNED_SQLSTATE;\n' +
                `SELECT CONCAT('${marker}:',IF(LEFT(@chat_state,2) IN('00','01','02'),0,@chat_errno));\n`;
            this.child.stdin.write(request);
        });
    }

    /** 取消未完成语句、请求客户端退出并等待关闭；一秒后仍未退出则终止。 */ async close() {
        if (this.pending) this.abort('MysqlSessionClosed');
        if (!this.exited) this.child.stdin.end('quit\n');
        const timer = setTimeout(/** 关闭宽限期结束后终止尚未退出的客户端。 */ () => { if (!this.exited) this.child.kill(); }, 1000);
        try { await this.closed; } finally { clearTimeout(timer); }
    }
}

const mysqlArgs = ['--batch', '--raw', '--skip-column-names', '--silent', '--unbuffered', '--force',
    '--connect-timeout=2', '--default-character-set=utf8mb4'];

module.exports = { MysqlSession, mysqlArgs };
