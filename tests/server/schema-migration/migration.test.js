'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { loadManifest, identifier } = require('../../../schema/SchemaMigration');
const { configuration, main } = require('../../../schema/migrate');

test('ordered checksummed SQL and native version rows stay aligned', () => {
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

test('local SQL drift cannot be loaded as an approved migration', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-migration-contract-'));
    try {
        const source = path.join(__dirname, '../../../schema');
        fs.cpSync(source, root, { recursive: true });
        fs.appendFileSync(path.join(root, 'migrations/001_baseline.sql'), '\n-- changed\n');
        assert.throws(() => loadManifest(root), /MigrationChecksumDrift/);
    } finally {
        assert.equal(path.dirname(fs.realpathSync(root)), fs.realpathSync(os.tmpdir()));
        assert.ok(path.basename(root).startsWith('chat-migration-contract-'));
        fs.rmSync(root, { recursive: true });
    }
});

test('migration identifier and configuration reject ambiguous or injected targets', async () => {
    for (const value of ['', 'a`b', 'db;DROP DATABASE db', '../db', 'a'.repeat(65)]) {
        assert.throws(() => identifier(value), /InvalidSchemaIdentifier/);
    }
    assert.throws(() => configuration({ CHAT_MYSQL_DATABASE: 'test' }), /InvalidMigrationConfiguration/);
    assert.throws(() => configuration({ CHAT_MYSQL_DATABASE: 'test', CHAT_MYSQL_HOST: 'localhost',
        CHAT_MYSQL_USER: 'test', CHAT_MYSQL_PASSWORD: '', CHAT_MYSQL_PORT: '65536' }), /InvalidMigrationConfiguration/);
    await assert.rejects(main({}, 'reset'), /ExpectedInspectPlanApplyOrVerify/);
});
