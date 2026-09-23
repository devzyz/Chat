'use strict';

const { startLocalMysql } = require('./localMysql');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
const { SchemaMigration } = require('../../../schema/SchemaMigration');
const { runCases } = require('./runMigrationCases');

/** 在自建本机 MySQL 实例执行迁移合同或显式诊断动作，结束后关闭会话与夹具。 */
async function main() {
    const fixture = await startLocalMysql(process.env.CHAT_MYSQL_BIN);
    const session = new MysqlSession(fixture.mysql, [...fixture.args, ...mysqlArgs], fixture.env);
    try {
        await session.execute('CREATE DATABASE schema_capture');
        const migration = new SchemaMigration(session, 'schema_capture');
        if (!process.argv.includes('--capture-contract') && !process.argv.includes('--diagnose')) {
            await session.close();
            await runCases(/** 为自建实例创建独立 mysql 会话。 */ () => new MysqlSession(fixture.mysql, [...fixture.args, ...mysqlArgs], fixture.env),
                'schema_capture', /** 执行单项合同后输出真实通过结果。 */ async (id, name, body) => {
                    await body();
                    console.log(`${id} PASS ${name}`);
                });
            return;
        }
        if (process.argv.includes('--capture-contract')) {
            try { await migration.apply(); } catch (error) { if (error.message !== 'SchemaContractDrift') throw error; }
            console.log(JSON.stringify(await migration.fingerprint(), null, 2));
        } else {
            console.log(JSON.stringify(await migration.apply()));
        }
        if (process.argv.includes('--diagnose')) {
            console.log(`counter=${await session.execute('SELECT COUNT(*),MAX(id) FROM user_id')}`);
            await session.execute('DROP PROCEDURE reg_user');
            const routine = migration.manifest.migrations[1].statements.at(-1)
                .replace("SET result = -1;\n    END;", 'RESIGNAL;\n    END;');
            await session.execute(routine);
        }
        await session.execute("CALL reg_user('test','synthetic@example.invalid','synthetic',@result)");
        console.log(`registered_uid=${await session.execute('SELECT @result')}`);
    } finally { await session.close(); await fixture.close(); }
}

main().catch(/** 输出失败原因并设置非零退出状态。 */ error => { console.error(error.message); process.exitCode = 1; });
