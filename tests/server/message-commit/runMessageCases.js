'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { randomBytes } = require('node:crypto');
const { SchemaMigration, identifier } = require('../../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');

/** 运行绝对路径的所属原生测试程序，限制总时长和输出并仅保留安全诊断。 */
function runNative(binary, env) {
    assert.ok(binary && path.isAbsolute(binary), 'absolute owned message binary required');
    // 启动原生提交合同进程并安装期限、输出及退出处理。
    return new Promise((resolve) => {
        const childEnv = { ...env,
            ...(process.platform === 'linux' ? { LD_LIBRARY_PATH: path.dirname(binary) } : {}) };
        delete childEnv.LD_PRELOAD;
        const child = spawn(binary, ['integration'], { env: childEnv,
        windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = '';
        let failed = false;
        let diagnostic = '';
        // 超过运行期限后标记失败并终止所属子进程。
        const timer = setTimeout(() => { failed = true; child.kill('SIGKILL'); }, 45000);
        // 进程启动错误时标记执行失败。
        child.on('error', () => { failed = true; });
        // 仅从标准错误提取白名单 SQL 错误码或测试失败标记。
        child.stderr.on('data', chunk => {
            diagnostic = chunk.toString().match(/message_commit_(?:sql_error_\d+|test_failed)/)?.[0] || diagnostic;
        });
        // 累计测试标记输出，超过上限则失败并终止所属进程。
        child.stdout.on('data', chunk => {
            output += chunk.toString().replace(/\r/g, '');
            if (output.length > 8192) { failed = true; child.kill('SIGKILL'); }
        });
        // 清除期限并返回退出结果及安全诊断。
        child.on('close', code => { clearTimeout(timer); resolve({ output, diagnostic: diagnostic || `native_exit_${code}`,
            passed: !failed && code === 0 }); });
    });
}

/** 创建独立数据库并迁移后运行原生提交合同，逐项记录结果并清理所属数据库。 */
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
        await new SchemaMigration(session, database).apply();
        result = await runNative(binary, { ...environment, CHAT_MYSQL_DATABASE: database });
        for (let index = 1; index < 20; index++) {
            const id = `T10-MSG-${String(index).padStart(2, '0')}`;
            // 验证原生进程输出包含该合同的真实通过标记。
            await record(id, 'production message transaction contract', async () => {
                assert.ok(result.output.includes(`PASS ${id}\n`), `${id} ${result.diagnostic || 'native process failed'}`);
            });
        }
    } catch (error) { primary = error; throw error; }
    finally {
        try {
            // 删除所属测试数据库并验证原生关闭存储合同及进程正常退出。
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

/** 用协调器拥有的 MySQL 容器和测试环境运行消息提交合同。 */
async function runMessageCases(coordinator, record) {
    assert.ok(coordinator.owned.has('mysql'), 'owned MySQL service required');
    const env = { ...process.env, MYSQL_PWD: coordinator.password,
        CHAT_MYSQL_HOST: coordinator.config.host, CHAT_MYSQL_PORT: String(coordinator.config.ports.mysql),
        CHAT_MYSQL_USER: 'root', CHAT_MYSQL_PASSWORD: coordinator.password };
    // 创建容器内独立 SQL 会话用于测试库准备和清理。
    return runCases(() => new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD',
        coordinator.config.ids.mysql, 'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET',
        '--user=root', ...mysqlArgs], env),
    process.env.CHAT_MESSAGE_TEST_BINARY, env, record, `${coordinator.config.database.slice(0, 56)}_message`);
}

module.exports = { runCases, runMessageCases };
