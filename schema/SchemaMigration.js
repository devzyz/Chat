'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

/** 校验数据库标识符的字符集与长度，返回反引号引用；非法名称抛异常。 */ function identifier(value) {
    if (!/^[A-Za-z][A-Za-z0-9_]{0,63}$/.test(value)) throw new Error('InvalidSchemaIdentifier');
    return `\`${value}\``;
}

/** 读取有序迁移清单并校验格式及 SQL 校验和；漂移时拒绝加载。 */ function loadManifest(root = __dirname) {
    const manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json'), 'utf8'));
    if (manifest.format !== 1 || !Array.isArray(manifest.migrations) || !manifest.migrations.length) {
        throw new Error('InvalidMigrationManifest');
    }
    manifest.migrations.forEach(/** 校验单项迁移身份和恢复说明，验证文件摘要后按约定分隔符提取语句。 */ (entry, index) => {
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
            .map(/** 去除 SQL 语句两端空白，后续过滤空语句。 */ sql => sql.trim()).filter(Boolean);
    });
    return manifest;
}

/** 在调用方提供的单会话上检查和迁移指定数据库；部分 DDL 失败必须显式恢复。 */ class SchemaMigration {
    /** 借用数据库会话，校验目标名称并加载已核验的迁移清单。 */ constructor(session, database, root = __dirname) {
        this.session = session;
        this.database = database;
        this.quoted = identifier(database);
        this.root = root;
        this.manifest = loadManifest(root);
    }

    /** 查询目标库的表、版本记录和兼容性标记，不执行 DDL。 */ async inspect() {
        await this.session.execute(`USE ${this.quoted}`);
        const tables = (await this.session.execute(
            'SELECT table_name FROM information_schema.tables WHERE table_schema=DATABASE() ORDER BY table_name'))
            .split('\n').filter(Boolean);
        const versions = tables.includes('schema_version') ? (await this.session.execute(
            'SELECT version, checksum, state FROM schema_version ORDER BY version')).split('\n').filter(Boolean)
            .map(/** 将版本查询的制表符行转换为版本、摘要及状态对象。 */ row => { const [version, checksum, state] = row.split('\t');
                return { version: Number(version), checksum, state }; }) : [];
        return { tables, versions, compatibility: this.manifest.compatibility };
    }

    /** 核对已应用历史后返回尚未应用的迁移；未知版本、漂移或部分失败均拒绝。 */ async plan() {
        const state = await this.inspect();
        if (!state.tables.includes('schema_version') && state.tables.length) {
            throw new Error('UnversionedDatabaseRequiresReviewedImport');
        }
        state.versions.forEach(/** 逐条核对历史顺序、校验和及 applied 状态。 */ (row, index) => {
            const expected = this.manifest.migrations[index];
            if (!expected || row.version !== expected.id) throw new Error('UnknownSchemaVersion');
            if (row.checksum !== expected.checksum) throw new Error('AppliedChecksumDrift');
            if (row.state !== 'applied') throw new Error('PartialMigrationRequiresRecovery');
        });
        return this.manifest.migrations.slice(state.versions.length);
    }

    /** 取得两秒有界会话锁后顺序迁移并验证；记录失败状态，释放锁且保留主异常。 */ async apply() {
        await this.session.execute(`USE ${this.quoted}`);
        const lock = `chat_schema_${crypto.createHash('sha256').update(this.database).digest('hex').slice(0,40)}`;
        if (await this.session.execute(`SELECT GET_LOCK('${lock}', 2)`) !== '1') {
            throw new Error('MigrationLockUnavailable');
        }
        let primary;
        try {
            const plan = await this.plan();
            if (!(await this.inspect()).tables.includes('schema_version')) await this.session.execute(`CREATE TABLE schema_version (
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
            return await this.verify();
        } catch (error) { primary = error; throw error; }
        finally {
            try {
                if (await this.session.execute(`SELECT RELEASE_LOCK('${lock}')`) !== '1') {
                    throw new Error('MigrationLockReleaseFailed');
                }
            } catch (cleanup) { if (!primary) throw cleanup; primary.cleanup = cleanup.message; }
        }
    }

    /** 确认无待应用迁移、元数据指纹和 UID 计数器有效；不修改数据库结构。 */ async verify() {
        if ((await this.plan()).length) throw new Error('SchemaVersionNotCurrent');
        const header = fs.readFileSync(path.join(this.root, 'SchemaContract.h'), 'utf8');
        const expected = header.match(/CONTRACT_HASH = "([a-f0-9]{64})"/)?.[1];
        const actual = await this.fingerprint();
        if (actual !== expected) throw new Error('SchemaContractDrift');
        if (await this.session.execute(`SELECT COUNT(*)=1 AND MIN(id)>=COALESCE((SELECT MAX(uid) FROM user),0)
            AND MAX(id)<=2147483647 FROM user_id`) !== '1') {
            throw new Error('InvalidUidCounter');
        }
        return { version: this.manifest.migrations.length, compatibility: this.manifest.compatibility };
    }

    /** 从权威 C++ 合同读取元数据查询并取得数据库指纹；调整当前会话聚合上限。 */ async fingerprint() {
        const header = fs.readFileSync(path.join(this.root, 'SchemaContract.h'), 'utf8');
        const query = header.match(/CONTRACT_QUERY = R"SQL\(([\s\S]*?)\)SQL";/)?.[1];
        if (!query) throw new Error('InvalidSchemaContract');
        await this.session.execute('SET SESSION group_concat_max_len=65536');
        return this.session.execute(query);
    }
}

module.exports = { SchemaMigration, loadManifest, identifier };
