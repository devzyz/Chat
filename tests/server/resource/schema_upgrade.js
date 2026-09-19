'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { startLocalMysql } = require('../schema-migration/localMysql');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');
const { SchemaMigration } = require('../../../schema/SchemaMigration');

async function main() {
    const fixture = await startLocalMysql(process.env.CHAT_MYSQL_BIN);
    const session = new MysqlSession(fixture.mysql, [...fixture.args, ...mysqlArgs], fixture.env);
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-resource-upgrade-'));
    try {
        // Reproduce the accepted version-2 contract using its unchanged migrations and fingerprint.
        const source = path.resolve(__dirname, '../../../schema');
        const manifest = JSON.parse(fs.readFileSync(path.join(source, 'manifest.json'), 'utf8'));
        manifest.migrations = manifest.migrations.slice(0, 2);
        fs.mkdirSync(path.join(root, 'migrations'));
        for (const entry of manifest.migrations) {
            fs.copyFileSync(path.join(source, entry.file), path.join(root, entry.file));
        }
        fs.writeFileSync(path.join(root, 'manifest.json'), JSON.stringify(manifest));
        const header = fs.readFileSync(path.join(source, 'SchemaContract.h'), 'utf8')
            .replace(/CONTRACT_HASH = "[a-f0-9]{64}"/,
                'CONTRACT_HASH = "ed537ccc940ce556b6838307a2229223841ee32aebe3dc62ae23e40855a42dab"');
        fs.writeFileSync(path.join(root, 'SchemaContract.h'), header);
        await session.execute('CREATE DATABASE resource_upgrade');
        assert.equal((await new SchemaMigration(session, 'resource_upgrade', root).Apply()).version, 2);
        await session.execute("CALL reg_user('upgrade_user','upgrade@example.invalid','fixture',@uid)");
        await session.execute("INSERT INTO chat_message(chat_id,send_id,recv_id,content,status,client_msg_uuid) " +
            "VALUES(1,1,2,'preserve existing message',0,'55555555-5555-4555-8555-555555555555')");
        const before = await session.execute('SELECT message_id,content,client_msg_uuid FROM chat_message');
        const current = new SchemaMigration(session, 'resource_upgrade');
        assert.deepEqual((await current.Plan()).map(entry => entry.id), [3]);
        assert.equal((await current.Apply()).version, 3);
        assert.equal((await current.Apply()).version, 3);
        assert.equal(await session.execute('SELECT message_id,content,client_msg_uuid FROM chat_message'), before);
        assert.equal(await session.execute('SELECT COUNT(*) FROM user'), '1');
        await session.execute('DROP TABLE user_avatar');
        await assert.rejects(current.Verify(), /SchemaContractDrift/);
        console.log('S05-RESOURCE-17 PASS version 2 to 3 preserves user/message data; repeat and drift checks pass');
    } finally {
        await session.close();
        await fixture.close();
        assert.equal(path.dirname(fs.realpathSync(root)), fs.realpathSync(os.tmpdir()));
        assert.ok(path.basename(root).startsWith('chat-resource-upgrade-'));
        fs.rmSync(root, { recursive: true });
    }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
