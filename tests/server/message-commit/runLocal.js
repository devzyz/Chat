'use strict';

const { startLocalMysql } = require('../schema-migration/localMysql');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
const { runCases } = require('./runMessageCases');

async function main() {
    const fixture = await startLocalMysql(process.env.CHAT_MYSQL_BIN);
    try {
        const env = { ...process.env, ...fixture.env, CHAT_MYSQL_HOST: '127.0.0.1',
            CHAT_MYSQL_PORT: String(fixture.port), CHAT_MYSQL_USER: 'root',
            CHAT_MYSQL_PASSWORD: fixture.env?.MYSQL_PWD || '' };
        await runCases(() => new MysqlSession(fixture.mysql, [...fixture.args, ...mysqlArgs], fixture.env),
            process.env.CHAT_MESSAGE_TEST_BINARY, env, async (id, name, body) => {
                await body(); console.log(`${id} PASS`);
            });
    } finally { await fixture.close(); }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
