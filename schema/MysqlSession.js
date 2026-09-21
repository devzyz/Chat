'use strict';

const { spawn } = require('node:child_process');
const { randomBytes } = require('node:crypto');
const os = require('node:os');
const path = require('node:path');

// One mysql client process is one server session: advisory locks survive DDL commits.
// --force is safe here because execute accepts ONE statement, then a marker SELECT.
// SQL diagnostics are reduced to numeric codes; raw SQL/credentials never escape.
class MysqlSession {
    constructor(file, args, env = process.env, timeoutMs = 10000) {
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
        this.closed = new Promise(resolve => { this.resolveClosed = resolve; });
        this.child.stdout.on('data', chunk => { this.output += chunk.toString().replace(/\r/g, ''); this.consume(); });
        this.child.stderr.on('data', chunk => {
            this.errors += chunk;
            if (this.errors.length > 65536) this.abort('MysqlOutputLimit');
        });
        this.child.stdin.on('error', () => this.abort('MysqlUnavailable'));
        this.child.on('error', () => this.abort('MysqlUnavailable'));
        this.child.on('close', () => {
            this.exited = true;
            this.fail('MysqlUnavailable');
            this.resolveClosed();
        });
    }

    fail(code) {
        if (!this.pending) return;
        const pending = this.pending;
        this.pending = null;
        clearTimeout(pending.timer);
        pending.reject(new Error(code));
    }

    abort(code) {
        this.fail(code);
        if (!this.exited) this.child.kill();
    }

    consume() {
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

    execute(sql) {
        if (this.exited || this.pending) return Promise.reject(new Error('MysqlSessionUnavailable'));
        const marker = `chat_end_${randomBytes(16).toString('hex')}`;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => this.abort('MysqlDeadlineExceeded'), this.timeoutMs);
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

    async close() {
        if (this.pending) this.abort('MysqlSessionClosed');
        if (!this.exited) this.child.stdin.end('quit\n');
        const timer = setTimeout(() => { if (!this.exited) this.child.kill(); }, 1000);
        try { await this.closed; } finally { clearTimeout(timer); }
    }
}

const mysqlArgs = ['--batch', '--raw', '--skip-column-names', '--silent', '--unbuffered', '--force',
    '--connect-timeout=2', '--default-character-set=utf8mb4'];

module.exports = { MysqlSession, mysqlArgs };
