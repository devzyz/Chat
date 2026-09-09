'use strict';

const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const { startLocalMysql } = require('./localMysql');

async function main() {
    const fixture = await startLocalMysql(process.env.CHAT_MYSQL_BIN);
    try {
        const source = await fs.readFile(process.argv[2], 'utf8');
        await fixture.execute('CREATE DATABASE registration_repro;');
        await fixture.execute(`USE registration_repro;\n${source}`);
        const result = await fixture.execute("USE registration_repro; CALL reg_user('synthetic','test@example.invalid','synthetic',@result); SELECT @result;");
        console.log(`original_reg_user_result=${result}; expected=positive_uid`);
        if (process.argv.includes('--diagnose')) {
            const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
            const session = new MysqlSession(fixture.mysql, [...fixture.args, ...mysqlArgs], fixture.env);
            try {
                await session.execute('USE registration_repro');
                for (const [name, statement] of [
                    ['wrong_column', "INSERT INTO user(uid,name,email,pwd) VALUES(1,'synthetic','test@example.invalid','synthetic')"],
                    ['missing_defaults', "INSERT INTO user(uid,name,email,password) VALUES(1,'synthetic','test@example.invalid','synthetic')"],
                    ['complete_fields', "INSERT INTO user(uid,name,email,password,description,icon,sex) VALUES(1,'synthetic','test@example.invalid','synthetic','','',0)"]
                ]) {
                    try { await session.execute(statement); console.log(`${name}=success`); }
                    catch (error) { console.log(`${name}=${error.message}`); }
                }
                console.log(`counter_rows=${await session.execute('SELECT COUNT(*) FROM user_id')}`);
            } finally { await session.close(); }
        }
        assert.ok(Number(result) > 0, 'exported registration does not create a user');
    } catch (error) {
        try { await fixture.close(); } catch (cleanup) { error.message += `; cleanup=${cleanup.message}`; }
        throw error;
    }
    await fixture.close();
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
