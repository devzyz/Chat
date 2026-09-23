'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { loadManifest, identifier } = require('../../../schema/SchemaMigration');
const { configuration, main } = require('../../../schema/migrate');

test('ordered checksummed SQL and native version rows stay aligned', /** 验证有序 SQL 校验和、版本记录及原生元数据合同保持一致。 */ () => {
    const manifest = loadManifest();
    const header = fs.readFileSync(path.join(__dirname, '../../../schema/SchemaContract.h'), 'utf8');
    assert.equal(manifest.compatibility, 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1');
    for (const entry of manifest.migrations) {
        assert.ok(header.includes(`${entry.id}:${entry.checksum}:applied`));
        assert.equal(entry.reversible, false);
        assert.ok(entry.recovery.includes('backup'));
    }
    assert.match(header, /CONTRACT_HASH = "[a-f0-9]{64}"/);
});

test('local SQL drift cannot be loaded as an approved migration', /** 验证本地 SQL 文件漂移不能作为已批准迁移加载。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-migration-contract-'));
    try {
        const source = path.join(__dirname, '../../../schema');
        fs.cpSync(source, root, { recursive: true });
        fs.appendFileSync(path.join(root, 'migrations/001_baseline.sql'), '\n-- changed\n');
        assert.throws(/** 加载变异迁移目录以确认摘要漂移拒绝。 */ () => loadManifest(root), /MigrationChecksumDrift/);
    } finally {
        assert.equal(path.dirname(fs.realpathSync(root)), fs.realpathSync(os.tmpdir()));
        assert.ok(path.basename(root).startsWith('chat-migration-contract-'));
        fs.rmSync(root, { recursive: true });
    }
});

test('migration identifier and configuration reject ambiguous or injected targets', /** 验证数据库标识符与迁移配置拒绝歧义、注入、缺项及非法动作。 */ async () => {
    for (const value of ['', 'a`b', 'db;DROP DATABASE db', '../db', 'a'.repeat(65)]) {
        assert.throws(/** 校验当前非法数据库名称。 */ () => identifier(value), /InvalidSchemaIdentifier/);
    }
    assert.throws(/** 校验缺少连接配置的环境。 */ () => configuration({ CHAT_MYSQL_DATABASE: 'test' }), /InvalidMigrationConfiguration/);
    assert.throws(/** 校验超出合法范围的连接端口。 */ () => configuration({ CHAT_MYSQL_DATABASE: 'test', CHAT_MYSQL_HOST: 'localhost',
        CHAT_MYSQL_USER: 'test', CHAT_MYSQL_PASSWORD: '', CHAT_MYSQL_PORT: '65536' }), /InvalidMigrationConfiguration/);
    await assert.rejects(main({}, 'reset'), /ExpectedInspectPlanApplyOrVerify/);
});
