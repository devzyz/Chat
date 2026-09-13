'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function identifier(value) {
    if (!/^[A-Za-z][A-Za-z0-9_]{0,63}$/.test(value)) throw new Error('InvalidSchemaIdentifier');
    return `\`${value}\``;
}

function loadManifest(root = __dirname) {
    const manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json'), 'utf8'));
    if (manifest.format !== 1 || !Array.isArray(manifest.migrations) || !manifest.migrations.length) {
        throw new Error('InvalidMigrationManifest');
    }
    manifest.migrations.forEach((entry, index) => {
        if (entry.id !== index + 1 || entry.version !== String(entry.id) ||
            !/^migrations\/[0-9]{3}_[a-z0-9_]+\.sql$/.test(entry.file) ||
            typeof entry.reversible !== 'boolean' || !entry.recovery ||
            !/^[a-f0-9]{64}$/.test(entry.checksum)) throw new Error('InvalidMigrationManifest');
        const file = path.join(root, entry.file);
        const bytes = fs.readFileSync(file);
        if (crypto.createHash('sha256').update(bytes).digest('hex') !== entry.checksum) {
            throw new Error('MigrationChecksumDrift');
        }
        entry.statements = bytes.toString('utf8').split(/^-- migration-statement\s*$/m)
            .map(sql => sql.trim()).filter(Boolean);
    });
    return manifest;
}

class SchemaMigration {
    constructor(session, database, root = __dirname) {
        this.session = session;
        this.database = database;
        this.quoted = identifier(database);
        this.root = root;
        this.manifest = loadManifest(root);
    }

    async Inspect() {
        await this.session.execute(`USE ${this.quoted}`);
        const tables = (await this.session.execute(
            'SELECT table_name FROM information_schema.tables WHERE table_schema=DATABASE() ORDER BY table_name'))
            .split('\n').filter(Boolean);
        const versions = tables.includes('schema_version') ? (await this.session.execute(
            'SELECT version, checksum, state FROM schema_version ORDER BY version')).split('\n').filter(Boolean)
            .map(row => { const [version, checksum, state] = row.split('\t');
                return { version: Number(version), checksum, state }; }) : [];
        return { tables, versions, compatibility: this.manifest.compatibility };
    }

    async Plan() {
        const state = await this.Inspect();
        if (!state.tables.includes('schema_version') && state.tables.length) {
            throw new Error('UnversionedDatabaseRequiresReviewedImport');
        }
        state.versions.forEach((row, index) => {
            const expected = this.manifest.migrations[index];
            if (!expected || row.version !== expected.id) throw new Error('UnknownSchemaVersion');
            if (row.checksum !== expected.checksum) throw new Error('AppliedChecksumDrift');
            if (row.state !== 'applied') throw new Error('PartialMigrationRequiresRecovery');
        });
        return this.manifest.migrations.slice(state.versions.length);
    }

    async Apply() {
        await this.session.execute(`USE ${this.quoted}`);
        const lock = `chat_schema_${crypto.createHash('sha256').update(this.database).digest('hex').slice(0,40)}`;
        if (await this.session.execute(`SELECT GET_LOCK('${lock}', 2)`) !== '1') {
            throw new Error('MigrationLockUnavailable');
        }
        let primary;
        try {
            const plan = await this.Plan();
            if (!(await this.Inspect()).tables.includes('schema_version')) await this.session.execute(`CREATE TABLE schema_version (
                version INT UNSIGNED NOT NULL PRIMARY KEY,
                checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
                state ENUM('applying','applied','failed') NOT NULL,
                applied_at TIMESTAMP NULL DEFAULT NULL
            ) ENGINE=InnoDB`);
            for (const entry of plan) {
                await this.session.execute(`INSERT INTO schema_version(version,checksum,state)
                    VALUES(${entry.id},'${entry.checksum}','applying')`);
                try {
                    for (const sql of entry.statements) await this.session.execute(sql);
                    await this.session.execute(`UPDATE schema_version SET state='applied',applied_at=CURRENT_TIMESTAMP
                        WHERE version=${entry.id}`);
                } catch (error) {
                    try { await this.session.execute(`UPDATE schema_version SET state='failed' WHERE version=${entry.id}`); }
                    catch (_) { /* A lost session retains applying; both states require explicit recovery. */ }
                    throw error;
                }
            }
            return await this.Verify();
        } catch (error) { primary = error; throw error; }
        finally {
            try {
                if (await this.session.execute(`SELECT RELEASE_LOCK('${lock}')`) !== '1') {
                    throw new Error('MigrationLockReleaseFailed');
                }
            } catch (cleanup) { if (!primary) throw cleanup; primary.cleanup = cleanup.message; }
        }
    }

    async Verify() {
        if ((await this.Plan()).length) throw new Error('SchemaVersionNotCurrent');
        const header = fs.readFileSync(path.join(this.root, 'SchemaContract.h'), 'utf8');
        const expected = header.match(/CONTRACT_HASH = "([a-f0-9]{64})"/)?.[1];
        const actual = await this.Fingerprint();
        if (actual !== expected) throw new Error('SchemaContractDrift');
        if (await this.session.execute(`SELECT COUNT(*)=1 AND MIN(id)>=COALESCE((SELECT MAX(uid) FROM user),0)
            AND MAX(id)<=2147483647 FROM user_id`) !== '1') {
            throw new Error('InvalidUidCounter');
        }
        return { version: this.manifest.migrations.length, compatibility: this.manifest.compatibility };
    }

    async Fingerprint() {
        const header = fs.readFileSync(path.join(this.root, 'SchemaContract.h'), 'utf8');
        const query = header.match(/CONTRACT_QUERY = R"SQL\(([\s\S]*?)\)SQL";/)?.[1];
        if (!query) throw new Error('InvalidSchemaContract');
        await this.session.execute('SET SESSION group_concat_max_len=65536');
        return this.session.execute(query);
    }
}

module.exports = { SchemaMigration, loadManifest, identifier };
