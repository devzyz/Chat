'use strict';

const { spawnSync } = require('node:child_process');
const { describe, source } = require('./contract');

// 运行前逐项核对 SQL 内容与基线清单。
function contract() {
    const description = describe();
    if (JSON.stringify(description.hashes) !== JSON.stringify(JSON.parse(source('manifest.json')))) {
        throw new Error('PgMigrationChecksumDrift');
    }
    return description;
}

// 全部判断和 DDL 使用同一 psql 会话；失败关闭会话即回滚。
function script(action) {
    if (!['inspect', 'plan', 'apply', 'verify'].includes(action)) throw new Error('ExpectedInspectPlanApplyOrVerify');
    const { sql, fingerprint, hash } = contract();
    const lines = [
        action === 'apply' ? 'BEGIN;' : 'BEGIN READ ONLY;',
        "SET LOCAL search_path = pg_catalog, chat;",
        "SET LOCAL statement_timeout = '45s';",
        "SET LOCAL lock_timeout = '2s';",
    ];
    if (action === 'apply') lines.push('SELECT pg_advisory_xact_lock(1128812884,1);');
    lines.push("SELECT to_regnamespace('chat') IS NOT NULL AS installed \\gset", '\\if :installed',
        fingerprint + ' \\gset',
        "SELECT count(*)=1 AND bool_and(version=1 AND checksum='" + hash +
        "' AND catalog=:'fingerprint') AND (SELECT count(*)=1 AND min(id)>=coalesce((SELECT max(uid) " +
        "FROM chat.account),0) FROM chat.user_id) AS valid FROM chat.schema_version \\gset",
        '\\if :valid', "SELECT 'current';", '\\else', "DO $$ BEGIN RAISE EXCEPTION 'PgSchemaDrift'; END $$;", '\\endif',
        '\\else',
        "SELECT NOT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname NOT LIKE 'pg\\_%' " +
        "AND nspname NOT IN ('public','information_schema')) " +
        "AND NOT EXISTS (SELECT 1 FROM pg_class WHERE relnamespace='public'::regnamespace) " +
        "AND NOT EXISTS (SELECT 1 FROM pg_proc WHERE pronamespace='public'::regnamespace) " +
        "AND NOT EXISTS (SELECT 1 FROM pg_type WHERE typnamespace='public'::regnamespace) AS empty \\gset",
        '\\if :empty');
    if (action === 'apply') {
        lines.push('CREATE SCHEMA chat;', 'SET LOCAL search_path = chat, pg_catalog;', sql,
            'CREATE TABLE schema_version (version integer PRIMARY KEY, checksum text NOT NULL, catalog text NOT NULL);',
            'SET LOCAL search_path = pg_catalog, chat;', fingerprint + ' \\gset',
            "INSERT INTO chat.schema_version VALUES (1,'" + hash + "',:'fingerprint');", "SELECT 'applied';");
    } else if (action === 'verify') {
        lines.push("DO $$ BEGIN RAISE EXCEPTION 'PgSchemaMissing'; END $$;");
    } else {
        lines.push(action === 'plan' ? "SELECT 'pending';" : "SELECT 'empty';");
    }
    lines.push('\\else', "DO $$ BEGIN RAISE EXCEPTION 'PgNonemptyDatabase'; END $$;", '\\endif', '\\endif', 'COMMIT;');
    return lines.join('\n') + '\n';
}

// 显式指定目标实例；密码只从子进程环境传递，错误输出只公布固定类别。
function migrate(env, action) {
    const database = env.CHAT_PG_DATABASE;
    const port = Number(env.CHAT_PG_PORT || 5432);
    if (!/^[a-zA-Z_][a-zA-Z_0-9]{0,62}$/.test(database || '') || !env.CHAT_PG_HOST ||
        !env.CHAT_PG_USER || !Number.isInteger(port) || port < 1 || port > 65535 || env.CHAT_PG_PASSWORD === undefined) {
        throw new Error('InvalidMigrationConfiguration');
    }
    const args = ['-X', '-w', '-qAt', '-v', 'ON_ERROR_STOP=1', '-h', env.CHAT_PG_HOST, '-p', String(port),
        '-U', env.CHAT_PG_USER, '-d', database, '-f', '-'];
    const result = spawnSync(env.CHAT_PG_CLIENT || 'psql', args, { encoding: 'utf8', input: script(action),
        windowsHide: true, timeout: 60000, maxBuffer: 1048576,
        env: { ...env, PGCONNECT_TIMEOUT: '3', PGCLIENTENCODING: 'UTF8', PGPASSWORD: env.CHAT_PG_PASSWORD } });
    if (result.error || result.status !== 0) {
        const code = /PgSchemaDrift|PgSchemaMissing|PgNonemptyDatabase/.exec(result.stderr || '');
        throw new Error(code ? code[0] : 'PgMigrationFailed');
    }
    const state = result.stdout.trim();
    return { backend: 'postgresql', state, version: state === 'current' || state === 'applied' ? 1 : 0, targetVersion: 1 };
}

module.exports = { migrate };
