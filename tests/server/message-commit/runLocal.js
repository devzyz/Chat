'use strict';

const { startLocalMysql } = require('../schema-migration/localMysql');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
const { runCases } = require('./runMessageCases');

/** 使用本机独立 MySQL 夹具运行真实消息提交合同并保证实例清理。 */
async function main() {
    const fixture = await startLocalMysql(process.env.CHAT_MYSQL_BIN);
    try {
        const env = { ...process.env, ...fixture.env, CHAT_MYSQL_HOST: '127.0.0.1',
            CHAT_MYSQL_PORT: String(fixture.port), CHAT_MYSQL_USER: 'root',
            CHAT_MYSQL_PASSWORD: fixture.env?.MYSQL_PWD || '' };
        await runCases(/** 为独立 MySQL 夹具创建 SQL 会话。 */ () => new MysqlSession(fixture.mysql, [...fixture.args, ...mysqlArgs], fixture.env),
            process.env.CHAT_MESSAGE_TEST_BINARY, env, /** 运行单项合同并仅在成功后发布通过标记。 */ async (id, name, body) => {
                await body(); console.log(`${id} PASS`);
            });
    } finally { await fixture.close(); }
}

main().catch(/** 输出合同失败原因并设置非零退出状态。 */ error => { console.error(error.message); process.exitCode = 1; });
