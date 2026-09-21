'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { randomBytes } = require('node:crypto');
const { SchemaMigration, identifier } = require('../../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');

function runNative(binary, env) {
    assert.ok(binary && path.isAbsolute(binary), 'absolute owned message binary required');
    return new Promise((resolve) => {
        const childEnv = { ...env,
            ...(process.platform === 'linux' ? { LD_LIBRARY_PATH: path.dirname(binary) } : {}) };
        delete childEnv.LD_PRELOAD;
        const child = spawn(binary, ['integration'], { env: childEnv,
        windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = '';
        let failed = false;
        let diagnostic = '';
        const timer = setTimeout(() => { failed = true; child.kill('SIGKILL'); }, 45000);
        child.on('error', () => { failed = true; });
        child.stderr.on('data', chunk => {
            diagnostic = chunk.toString().match(/message_commit_(?:sql_error_\d+|test_failed)/)?.[0] || diagnostic;
        });
        child.stdout.on('data', chunk => {
            output += chunk.toString().replace(/\r/g, '');
            if (output.length > 8192) { failed = true; child.kill('SIGKILL'); }
        });
        child.on('close', code => { clearTimeout(timer); resolve({ output, diagnostic: diagnostic || `native_exit_${code}`,
            passed: !failed && code === 0 }); });
    });
}

async function runCases(createSession, binary, environment, record,
    database = `chat_message_${randomBytes(10).toString('hex')}_message`) {
    const quoted = identifier(database);
    const session = createSession();
    let created = false;
    let primary;
    let result;
    try {
        await session.execute(`CREATE DATABASE ${quoted}`);
        created = true;
        await new SchemaMigration(session, database).Apply();
        result = await runNative(binary, { ...environment, CHAT_MYSQL_DATABASE: database });
        for (let index = 1; index < 20; index++) {
            const id = `T10-MSG-${String(index).padStart(2, '0')}`;
            await record(id, 'production message transaction contract', async () => {
                assert.ok(result.output.includes(`PASS ${id}\n`), `${id} ${result.diagnostic || 'native process failed'}`);
            });
        }
    } catch (error) { primary = error; throw error; }
    finally {
        try {
            await record('T10-MSG-20', 'closed storage rejects and owned database cleanup', async () => {
                if (created) {
                    await session.execute(`DROP DATABASE ${quoted}`);
                    assert.equal(await session.execute(
                        `SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${database}'`), '0');
                }
                assert.ok(result?.output.includes('PASS T10-MSG-20\n') && result.passed,
                    'native closed-storage contract or process exit failed');
            });
        } catch (cleanup) {
            if (!primary) throw cleanup;
            primary.cleanup = cleanup.message;
        } finally { await session.close(); }
    }
}

async function runMessageCases(coordinator, record) {
    assert.ok(coordinator.owned.has('mysql'), 'owned MySQL service required');
    const env = { ...process.env, MYSQL_PWD: coordinator.password,
        CHAT_MYSQL_HOST: coordinator.config.host, CHAT_MYSQL_PORT: String(coordinator.config.ports.mysql),
        CHAT_MYSQL_USER: 'root', CHAT_MYSQL_PASSWORD: coordinator.password };
    return runCases(() => new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD',
        coordinator.config.ids.mysql, 'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET',
        '--user=root', ...mysqlArgs], env),
    process.env.CHAT_MESSAGE_TEST_BINARY, env, record, `${coordinator.config.database.slice(0, 56)}_message`);
}

module.exports = { runCases, runMessageCases };
