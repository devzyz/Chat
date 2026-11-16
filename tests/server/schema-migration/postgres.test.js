'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const { postgresFixture, portNumber } = require('./localPostgres');
const { main } = require('../../../schema/migrate');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
const { SchemaMigration } = require('../../../schema/SchemaMigration');
const { startLocalMysql } = require('./localMysql');
const { backendWire } = require('./backendWire');

// 两个真实实例分别计算比较结果；对外行为以源库的相等语义为准。
async function compareIdentities(pg, mysql) {
    const pairs = [['ß','ss'],['ü','ue'],['a\u0308','ä'],['a\u0308','a'],['😀','😁'],['a\u0000','A'],
        ['full','ＦＵＬＬ'],['a\u2009','a'],['a ','A'],['ab','a b'],['甲','乙'],['é','E'],['🅰','A']];
    // 把测试输入编码为十六进制字节。
    const hex = value => Buffer.from(value).toString('hex');
    // 使用源库实际排序规则参与比较。
    const source = value => "CONVERT(UNHEX('" + hex(value) + "') USING utf8mb4) COLLATE utf8mb4_german2_ci";
    // 通过目标库账号键比较。
    const target = value => "chat.account_key(decode('" + hex(value) + "','hex'))";
    // 生成固定夹具的逐对比较投影。
    const sql = fn => 'SELECT ' + pairs.map(
        // 每一列表示一对输入在所选驱动中的相等结果。
        ([a,b]) => '(' + fn(a) + '=' + fn(b) + ')').join(',') + ';';
    const expected = (await mysql.execute(sql(source))).split('\t');
    // 将两个客户端不同的布尔表示规范化。
    const actual = pg.query(sql(target)).split('|').map(value => value === 't' ? '1' : '0');
    assert.deepEqual(actual,expected);
}

// 穿过独立会话观察整批回滚、关系变更及原 UUID 重放。
function verifyPrivateText(pg) {
    pg.query('INSERT INTO chat.private_chat(chat_id,user1_id,user2_id) VALUES(11,31,32);');
    const a = '12345678-1234-1234-1234-111111111111';
    const b = '12345678-1234-1234-1234-222222222222';
    // 生成带原始正文字节的提交项。
    const item = (uuid,body) => ({ uuid, body: Buffer.from(body).toString('hex') });
    // 一次提交整个测试批次。
    const commit = (messages,revision = 1) => pg.query("SELECT * FROM chat.commit_text(31,32,11," +
        revision + ",0,'" + JSON.stringify(messages) + "');");
    const original = item(a,'字\u0000节');
    const [id,uuid,created,at] = commit([original]).split('|');
    assert.equal(uuid,a);
    assert.equal(created,'t');
    assert.equal(commit([original]),[id,a,'f',at].join('|'));
    // 第二项冲突必须回滚首项。
    assert.throws(() => commit([item(b,'new'),item(a,'changed')]),/Conflict/);
    assert.equal(pg.query('SELECT count(*) FROM chat.chat_message;'),'1');
    pg.query('UPDATE chat.private_chat SET relationship_active=false,relationship_revision=2;');
    assert.equal(commit([original]),[id,a,'f',at].join('|'));
    // 关系删除后拒绝新增 UUID。
    assert.throws(() => commit([item(b,'new')]),/InvalidMembership/);
    pg.query('UPDATE chat.private_chat SET relationship_active=true,relationship_revision=3;');
    // 旧客户端版本不能向恢复的关系发送新消息。
    assert.throws(() => commit([item(b,'new')],0),/InvalidMembership/);
    assert.equal(commit([original,item(b,'new')],3).split('\n').length,2);
}

// 贯通真实数据库初始化、行为对比和版本校验。
test('PostgreSQL bootstrap, account equivalence and private text transactions', async () => {
    const pg = await postgresFixture();
    try {
        assert.equal((await main(pg.env,'plan')).state,'pending');
        await assert.rejects(main(pg.env,'verify'),/PgSchemaMissing/);
        pg.query("CREATE FUNCTION public.unowned() RETURNS int LANGUAGE SQL AS 'SELECT 1';");
        await assert.rejects(main(pg.env,'apply'),/PgNonemptyDatabase/);
        assert.equal(pg.query("SELECT public.unowned();"),'1');
        pg.query("DROP FUNCTION public.unowned();");
        assert.equal((await main(pg.env,'apply')).state,'applied');
        assert.equal((await main(pg.env,'apply')).state,'current');
        assert.equal((await main(pg.env,'verify')).state,'current');
        const mysql = await startLocalMysql(process.env.CHAT_MYSQL_BIN);
        try {
            await compareIdentities(pg,mysql);
            if (process.env.CHAT_PG_CONTRACT_BINARY) {
                await mysql.execute('CREATE DATABASE chat_pg_reference');
                const session = new MysqlSession(mysql.mysql,[...mysql.args,...mysqlArgs],mysql.env);
                try { await new SchemaMigration(session,'chat_pg_reference').apply(); }
                finally { await session.close(); }
                await mysql.execute("USE chat_pg_reference; CALL reg_user('Alice','one@x','hash',@u); " +
                    "CALL reg_user('Bob','two@x','hash',@u);");
                execFileSync(process.env.CHAT_PG_CONTRACT_BINARY,[],{
                    env: { ...pg.env, CHAT_MYSQL_FIXTURE_PORT: String(mysql.port) },
                    windowsHide: true, timeout: 45000, stdio: ['ignore','pipe','pipe']
                });
                if (process.env.CHAT_DB_SERVICE_BIN) {
                    await backendWire({driver:'mysql',name:'chat_pg_reference',user:'root',password:'',port:mysql.port},portNumber);
                }
            }
        } finally { await mysql.close(); }
        assert.equal(pg.query("SELECT chat.reg_user('Alice','one@x','hash');"),'1');
        assert.equal(pg.query("SELECT chat.reg_user('alice ','two@x','hash');"),'0');
        assert.equal(pg.query("SELECT chat.reg_user('Bob','two@x','hash');"),'2');
        verifyPrivateText(pg);
        if (process.env.CHAT_PG_CONTRACT_BINARY) {
            execFileSync(process.env.CHAT_PG_CONTRACT_BINARY,[],{
                env: pg.env, windowsHide: true, timeout: 45000, stdio: ['ignore','pipe','pipe']
            });
        }
        if (process.env.CHAT_DB_SERVICE_BIN) {
            await backendWire({driver:'postgresql',name:pg.env.CHAT_PG_DATABASE,user:pg.env.CHAT_PG_USER,
                password:pg.env.CHAT_PG_PASSWORD,port:Number(pg.env.CHAT_PG_PORT)},portNumber);
        }
        pg.query('DROP INDEX chat.private_first;');
        await assert.rejects(main(pg.env,'verify'),/PgSchemaDrift/);
        await assert.rejects(main(pg.env,'apply'),/PgSchemaDrift/);
    } finally { pg.close(); }
});
