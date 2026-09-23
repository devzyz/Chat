'use strict';

const assert = require('node:assert/strict');
const { SchemaMigration, identifier } = require('../../../schema/SchemaMigration');
const { MysqlSession, mysqlArgs } = require('../../../schema/MysqlSession');

/** 通过参数化存储过程注册测试用户并返回 UID，保证释放预处理语句。 */
async function registration(session, name, email) {
    // Fixture values are bound using MySQL PREPARE/EXECUTE, never interpolated as SQL text.
    const encode = /** 把测试字符串编码为显式 UTF-8 十六进制 SQL 值。 */ value => `CONVERT(0x${Buffer.from(value).toString('hex')} USING utf8mb4)`;
    await session.execute("PREPARE register_fixture FROM 'CALL reg_user(?,?,?,@registered_uid)'");
    try {
        await session.execute(`SET @reg_name=${encode(name)},@reg_email=${encode(email)},@reg_password=${encode('synthetic')}`);
        await session.execute('EXECUTE register_fixture USING @reg_name,@reg_email,@reg_password');
        return Number(await session.execute('SELECT @registered_uid'));
    } finally { await session.execute('DEALLOCATE PREPARE register_fixture'); }
}

/** 在调用方拥有的数据库执行迁移、漂移及并发注册合同，清理自建辅助库并保留主异常。 */
async function runCases(createSession, database, record) {
    const session = createSession();
    const auxiliary = `${database.slice(0,54)}_migration`;
    identifier(auxiliary);
    let created = false;
    let primary;
    const migration = new SchemaMigration(session, database);
    const routine = migration.manifest.migrations[1].statements.at(-1);
    try {
        await record('T10-MIG-01', 'fresh migration reaches current N', /** 验证空库迁移到当前版本且表集合完整。 */ async () => {
            assert.equal((await migration.apply()).version, 4);
            assert.equal((await migration.inspect()).tables.length, 15);
        });
        await record('T10-MIG-02', 'repeat application preserves registered user', /** 验证重复迁移幂等并保留已注册用户。 */ async () => {
            const uid = await registration(session, 'migration_seed', 'migration_seed@example.invalid');
            assert.ok(uid > 0);
            const before = await session.execute('SELECT COUNT(*),MAX(uid) FROM user');
            assert.equal((await migration.apply()).version, 4);
            assert.equal(await session.execute('SELECT COUNT(*),MAX(uid) FROM user'), before);
        });
        await record('T10-MIG-03', 'applied checksum drift fails closed', /** 验证已应用校验和漂移拒绝迁移，随后恢复夹具值。 */ async () => {
            const original = migration.manifest.migrations[0].checksum;
            await session.execute("UPDATE schema_version SET checksum=REPEAT('0',64) WHERE version=1");
            try { await assert.rejects(migration.apply(), /AppliedChecksumDrift/); }
            finally { await session.execute(`UPDATE schema_version SET checksum='${original}' WHERE version=1`); }
        });
        await record('T10-MIG-04', 'unknown version cannot start or migrate', /** 验证未知版本同时阻断验证和迁移，随后删除注入记录。 */ async () => {
            await session.execute("INSERT INTO schema_version VALUES(999,REPEAT('0',64),'applied',CURRENT_TIMESTAMP)");
            try {
                await assert.rejects(migration.verify(), /UnknownSchemaVersion/);
                await assert.rejects(migration.apply(), /UnknownSchemaVersion/);
            } finally { await session.execute('DELETE FROM schema_version WHERE version=999'); }
        });
        await record('T10-MIG-05', 'missing message uniqueness index is rejected', /** 验证缺失 UUID 唯一索引导致合同漂移，随后恢复索引。 */ async () => {
            await session.execute('ALTER TABLE chat_message DROP INDEX unique_sender_client_uuid');
            try { await assert.rejects(migration.verify(), /SchemaContractDrift/); }
            finally { await session.execute('ALTER TABLE chat_message ADD UNIQUE INDEX unique_sender_client_uuid(send_id,client_msg_uuid)'); }
        });
        await record('T10-MIG-06', 'missing routine is rejected and restored explicitly', /** 验证缺失注册过程导致合同漂移，随后恢复过程。 */ async () => {
            await session.execute('DROP PROCEDURE reg_user');
            try { await assert.rejects(migration.verify(), /SchemaContractDrift/); }
            finally { await session.execute(routine); }
        });
        await record('T10-MIG-07', 'parallel same registration creates one user', /** 验证八个相同用户并发注册只有一次创建成功。 */ async () => {
            const results = await Promise.all(Array.from({ length: 8 }, /** 在独立会话尝试注册相同身份并保证关闭。 */ async () => {
                const peer = createSession();
                try {
                    await peer.execute(`USE ${identifier(database)}`);
                    return await registration(peer, 'migration_same', 'migration_same@example.invalid');
                } finally { await peer.close(); }
            }));
            assert.equal(results.filter(/** 筛选成功创建用户的正 UID。 */ uid => uid > 0).length, 1);
            assert.equal(results.filter(/** 筛选已存在用户的零结果。 */ uid => uid === 0).length, 7);
        });
        await record('T10-MIG-08', 'parallel distinct registrations allocate distinct UIDs', /** 验证八个不同身份并发注册均获得唯一正 UID。 */ async () => {
            const results = await Promise.all(Array.from({ length: 8 }, /** 在独立会话注册带序号身份并保证关闭。 */ async (_, index) => {
                const peer = createSession();
                try {
                    await peer.execute(`USE ${identifier(database)}`);
                    return await registration(peer, `migration_distinct_${index}`, `migration_${index}@example.invalid`);
                } finally { await peer.close(); }
            }));
            assert.ok(results.every(/** 确认注册返回正 UID。 */ uid => uid > 0));
            assert.equal(new Set(results).size, 8);
        });
        await record('T10-MIG-09', 'missing UID seed is explicit failure without user insertion', /** 验证 UID 种子缺失时验证失败且注册不插入用户，随后恢复计数器。 */ async () => {
            const counter = await session.execute('SELECT id FROM user_id');
            const count = await session.execute('SELECT COUNT(*) FROM user');
            await session.execute('DELETE FROM user_id');
            try {
                await assert.rejects(migration.verify(), /InvalidUidCounter/);
                assert.equal(await registration(session, 'migration_missing', 'migration_missing@example.invalid'), -1);
                assert.equal(await session.execute('SELECT COUNT(*) FROM user'), count);
            } finally { await session.execute(`INSERT INTO user_id VALUES(${Number(counter)})`); }
        });
        await record('T10-MIG-10', 'partial DDL requires explicit empty-bootstrap recovery', /** 注入真实部分 DDL 失败并验证失败记录、盲重试拒绝及显式空库恢复。 */ async () => {
            await session.execute(`CREATE DATABASE ${identifier(auxiliary)}`);
            created = true;
            const failing = new SchemaMigration(session, auxiliary);
            // A real CREATE TABLE duplicate failure after earlier DDL has committed.
            failing.manifest.migrations[0].statements.splice(1, 0, 'CREATE TABLE apply_friend(id INT)');
            await assert.rejects(failing.apply(), /MysqlError:1050/);
            assert.equal(await session.execute('SELECT state FROM schema_version WHERE version=1'), 'failed');
            assert.equal(await session.execute("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='apply_friend'"), '1');
            await assert.rejects(new SchemaMigration(session, auxiliary).apply(), /PartialMigrationRequiresRecovery/);
            await session.execute(`DROP DATABASE ${identifier(auxiliary)}`);
            created = false;
            await session.execute(`CREATE DATABASE ${identifier(auxiliary)}`);
            created = true;
            const fresh = new SchemaMigration(session, auxiliary);
            assert.equal((await fresh.apply()).version, 4);
            const fingerprint = await fresh.fingerprint();
            await session.execute(`USE ${identifier(database)}`);
            assert.equal(await migration.fingerprint(), fingerprint);
        });
        await record('T10-MIG-11', 'routine body drift is rejected without repair on startup', /** 验证过程独立注释变化不影响合同而可执行正文变化被拒绝，随后恢复过程。 */ async () => {
            await session.execute('DROP PROCEDURE reg_user');
            try {
                await session.execute(routine.replace(
                    '-- All registrations take this one row lock before checking uniqueness.',
                    '-- A different explanatory comment must not change the schema contract.'));
                assert.equal((await migration.verify()).version, 4);
                await session.execute('DROP PROCEDURE reg_user');
                await session.execute(routine.replace('SET result = next_uid;', 'SET result = 42;'));
                await assert.rejects(migration.verify(), /SchemaContractDrift/);
            } finally {
                await session.execute('DROP PROCEDURE IF EXISTS reg_user');
                await session.execute(routine);
            }
            assert.equal((await migration.verify()).version, 4);
        });
    } catch (error) { primary = error; throw error; }
    finally {
        try {
            await record('T10-MIG-12', 'owned auxiliary database cleanup', /** 仅删除本测试创建的辅助数据库并确认已移除。 */ async () => {
                if (created) {
                    await session.execute(`DROP DATABASE ${identifier(auxiliary)}`);
                    created = false;
                }
                assert.equal(await session.execute(`SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name='${auxiliary}'`), '0');
            });
        } catch (cleanup) { if (!primary) throw cleanup; primary.cleanup = cleanup.message; }
        finally { await session.close(); }
    }
}

/** 要求协调器拥有 MySQL 容器，再以独立会话执行迁移合同。 */
async function runMigrationCases(coordinator, record) {
    assert.ok(coordinator.owned.has('mysql'));
    const createSession = /** 通过所属容器内的 mysql 创建会话，密码仅通过环境传入。 */ () => new MysqlSession('docker', ['exec', '-i', '--env', 'MYSQL_PWD',
        coordinator.config.ids.mysql, 'mysql', '--no-defaults', '--no-login-paths', '--protocol=SOCKET',
        '--host=localhost', '--user=root', ...mysqlArgs], { ...process.env, MYSQL_PWD: coordinator.password });
    return runCases(createSession, coordinator.config.database, record);
}

module.exports = { runMigrationCases, runCases, registration };
