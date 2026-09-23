'use strict';

const { MysqlSession, mysqlArgs } = require('./MysqlSession');
const { SchemaMigration, identifier } = require('./SchemaMigration');

/** 校验显式迁移环境配置，返回不含密码的连接目标；非法目标拒绝。 */ function configuration(env) {
    const database = env.CHAT_MYSQL_DATABASE;
    identifier(database || '');
    const port = Number(env.CHAT_MYSQL_PORT);
    if (!env.CHAT_MYSQL_HOST || !env.CHAT_MYSQL_USER || !Number.isInteger(port) || port < 1 || port > 65535 ||
        env.CHAT_MYSQL_PASSWORD === undefined) throw new Error('InvalidMigrationConfiguration');
    return { database, port, host: env.CHAT_MYSQL_HOST, user: env.CHAT_MYSQL_USER };
}

/** 执行白名单迁移动作，限制总时长并保证会话关闭；计划输出剔除 SQL 正文。 */ async function main(env = process.env, action = process.argv[2]) {
    const actions = { inspect: 'inspect', plan: 'plan', apply: 'apply', verify: 'verify' };
    if (!Object.hasOwn(actions, action)) throw new Error('ExpectedInspectPlanApplyOrVerify');
    const config = configuration(env);
    const session = new MysqlSession(env.CHAT_MYSQL_CLIENT || 'mysql', ['--no-defaults', '--protocol=TCP',
        `--host=${config.host}`, `--port=${config.port}`, `--user=${config.user}`, ...mysqlArgs],
    { ...env, MYSQL_PWD: env.CHAT_MYSQL_PASSWORD });
    const timer = setTimeout(/** 迁移达到总期限时中止会话。 */ () => session.abort('MigrationDeadlineExceeded'), 60000);
    try {
        const result = await new SchemaMigration(session, config.database)[actions[action]]();
        // Plan publishes metadata, not executable SQL bodies or any connection configuration.
        return action === 'plan' ? result.map(/** 只公开迁移元数据，移除可执行 SQL 语句。 */ ({ statements, ...entry }) => entry) : result;
    } finally { clearTimeout(timer); await session.close(); }
}

if (require.main === module) main().then(/** 将成功结果序列化到标准输出。 */ result => console.log(JSON.stringify(result))).catch(/** 仅公开安全错误码并设置失败退出状态。 */ error => {
    const safe = /^[A-Za-z][A-Za-z0-9:]{0,80}$/.test(error.message) ? error.message : 'SchemaMigrationFailed';
    console.error(safe);
    process.exitCode = 1;
});

module.exports = { main, configuration };
