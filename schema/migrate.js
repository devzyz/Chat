'use strict';

const { MysqlSession, mysqlArgs } = require('./MysqlSession');
const { SchemaMigration, identifier } = require('./SchemaMigration');

function configuration(env) {
    const database = env.CHAT_MYSQL_DATABASE;
    identifier(database || '');
    const port = Number(env.CHAT_MYSQL_PORT);
    if (!env.CHAT_MYSQL_HOST || !env.CHAT_MYSQL_USER || !Number.isInteger(port) || port < 1 || port > 65535 ||
        env.CHAT_MYSQL_PASSWORD === undefined) throw new Error('InvalidMigrationConfiguration');
    return { database, port, host: env.CHAT_MYSQL_HOST, user: env.CHAT_MYSQL_USER };
}

async function main(env = process.env, action = process.argv[2]) {
    const actions = { inspect: 'Inspect', plan: 'Plan', apply: 'Apply', verify: 'Verify' };
    if (!Object.hasOwn(actions, action)) throw new Error('ExpectedInspectPlanApplyOrVerify');
    const config = configuration(env);
    const session = new MysqlSession(env.CHAT_MYSQL_CLIENT || 'mysql', ['--no-defaults', '--protocol=TCP',
        `--host=${config.host}`, `--port=${config.port}`, `--user=${config.user}`, ...mysqlArgs],
    { ...env, MYSQL_PWD: env.CHAT_MYSQL_PASSWORD });
    const timer = setTimeout(() => session.abort('MigrationDeadlineExceeded'), 60000);
    try {
        const result = await new SchemaMigration(session, config.database)[actions[action]]();
        // Plan publishes metadata, not executable SQL bodies or any connection configuration.
        return action === 'plan' ? result.map(({ statements, ...entry }) => entry) : result;
    } finally { clearTimeout(timer); await session.close(); }
}

if (require.main === module) main().then(result => console.log(JSON.stringify(result))).catch(error => {
    const safe = /^[A-Za-z][A-Za-z0-9:]{0,80}$/.test(error.message) ? error.message : 'SchemaMigrationFailed';
    console.error(safe);
    process.exitCode = 1;
});

module.exports = { main, configuration };
