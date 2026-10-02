# MySQL schema and migration contract

The schema source is the user-supplied schema-only export of the historical Chat
development database (nine tables and `reg_user`). The raw export, its host metadata,
and its former AUTO_INCREMENT counters are not shipped. Executable ownership is
[`schema/migrations`](../schema/migrations), with ordered SHA-256 checksums and
recovery descriptions in [`manifest.json`](../schema/manifest.json).

## 基础社交迁移 007

`007_basic_social.sql` 增加 `user.profile_revision`、`apply_friend.revision`、
`private_chat.relationship_active/relationship_revision`。不新建平行用户、关系或消息表。
已有私聊默认有效、版本 1，保留原参与者私聊权限；删除只作关系墓碑，不删除消息及资源。
资料与关系写入按行锁、预期版本和数据库事务提交；创建/恢复关系按用户编号顺序锁定双方。
新消息与删除共用私聊行锁；旧 UUID 先确认身份，新 UUID 再核对当前关系。
迁移前备份、失败后的恢复边界及显式执行命令沿用下方 Operational entry；应用不会自动迁移运行库。

## Current schema

- `001_baseline.sql` preserves the exported table shapes, indexes, InnoDB engines,
  and `utf8mb4_german2_ci` collation. It creates no business records.
- `002_registration_message_identity.sql` adds unique user UID/name/email indexes,
  the required UID counter seed, the corrected registration procedure, and nullable
  `chat_message.client_msg_uuid CHAR(36) CHARACTER SET ascii COLLATE ascii_bin`
  with `UNIQUE(send_id, client_msg_uuid)`. Existing message rows retain NULL.
- Registration writes `password`, explicitly supplies empty description/icon and
  sex 0, serializes UID allocation through its one counter row, and has one explicit
  transaction. Success returns a positive signed-32-bit UID; a duplicate returns 0;
  storage/counter failure returns -1. Parameter collation is explicit, preserving
  the export's username/email comparison behavior independently of server defaults.
- The shared Node/native routine fingerprint excludes standalone `-- ` comment
  text, retaining the whitespace that MySQL 8.0 exposes. MySQL 8.4 retains these
  comments in metadata; this formatting difference must not reject the same schema.
  Executable body changes still fail verification, and migration checksums are unchanged.
- The unique constraints reject collisions; migration never silently deduplicates
  existing user records. Missing/multiple/stale UID counter rows fail verification.

- `003_avatar_resources.sql` owns published resources, message references and avatars.
- `004_message_receipts.sql` adds the per-conversation revision clock and current message receipt rows.
  Revisions are allocated under the same conversation lock as message commits and receipt page reads.
  Unique conversation/revision and message/recipient keys prevent duplicate facts. See [MessageStates](MessageStates.md).
  Schema 4 introduced receipts; the complete required version is defined by the manifest,
  including the group and basic-social migrations described here.

## Operational entry

Use Node 22+ and an existing MySQL 8 client. Set `CHAT_MYSQL_HOST`,
`CHAT_MYSQL_PORT`, `CHAT_MYSQL_USER`, `CHAT_MYSQL_DATABASE`, and
`CHAT_MYSQL_PASSWORD` in the process environment; optional `CHAT_MYSQL_CLIENT`
selects the executable. Do not put passwords into shell history or command arguments.
The database must already exist and be empty, or have this application's verified
migration history. The tool never creates or drops a deployment database.

```text
node schema/migrate.js inspect
node schema/migrate.js plan
node schema/migrate.js apply
node schema/migrate.js verify
```

`inspect` reports schema/history; `plan` refuses unknown versions, checksum drift,
partial history, and unversioned nonempty databases. `apply` uses a bounded advisory
lock on a single real session, records applying/applied/failed states in
`schema_version`, then verifies semantic table/column/index/routine/trigger metadata.
`verify` is read-only apart from session settings and fails on missing or altered
objects. Native Gate/Chat/Resource startup calls `chat_schema::Verify` from the same metadata
contract; it does not run DDL or require Node in the server runtime. Failure must
propagate before accepting application traffic.

The migrator needs DDL/DML privileges on the explicitly selected schema. The
application needs its normal CRUD/EXECUTE privileges plus visibility of routine
definitions for verification (MySQL `SHOW_ROUTINE` when it is not the routine owner).
`reg_user` uses SQL SECURITY INVOKER, not a shipped personal DEFINER. Routine
definitions in this schema contain no credentials. SQL/client diagnostics are
reduced to fixed error codes; migration output excludes configuration and secrets.

## Recovery and compatibility

MySQL DDL implicitly commits: a failed migration is **not** treated as rolled back.
Do not manually set a failed row to applied or rerun partial DDL. Inspect the primary
error, stop writers, and restore a verified pre-migration backup. For an explicitly
owned, empty disposable bootstrap database only, its fixture may drop/recreate it
and apply again. That recovery is exercised against a real partial CREATE failure.

No promoted N-1 schema/artifact exists in the supplied evidence;
`BOOTSTRAP_NO_PROMOTED_N_MINUS_1` remains open. The historical unversioned export is
not promoted N-1, and this tool intentionally refuses automatic in-place adoption.
Before moving an existing development/production database, back it up and create a
reviewed import/adoption plan with duplicate/counter preflight. Nothing here changes
the user's VM database.

`005_basic_groups.sql` corrects the exported `group_chat_member` primary key to
`(chat_id,user_id)` and adds `(user_id,chat_id)` for membership lookup. It adds nullable
`owner_uid`/`creation_uuid` and their unique index to `group_chat` for idempotent creation.
Existing rows remain intact. Group text uses `chat_message.recv_id=0` as an explicit
group sentinel; private messages keep positive recipients. All schema-verifying
Gate/Chat/Resource binaries must be upgraded together after migration to schema 7.
The group protocol is defined in [Protocol](Protocol.md#基础文字群聊).

`006_group_membership.sql` adds group revision/dissolved state and member state,
membership epoch and joined-after message boundary. Existing members become active
at epoch 1, boundary 0. A preflight CHECK rejects missing owners or contradictory
owner roles before altering the group tables; it never guesses an owner.
Immutable creator/original name and `group_creation_member` preserve creation retry
identity after later rename, transfer or membership changes. `group_operation`
stores actor/request identity, canonical request and original result in the same
transaction as each successful management operation. No history/resource bytes are deleted.

For executed evidence and follow-up scope use the main workspace `docs/Status.md`.
The [schema owner tests](../tests/server/schema-migration/README.md) are separate from
MessageCommit's native transaction/permission tests and four-process integration.

MySQL references: [implicit DDL commits](https://dev.mysql.com/doc/refman/8.4/en/implicit-commit.html),
[session advisory locks](https://dev.mysql.com/doc/refman/8.4/en/locking-functions.html),
[SQL diagnostics](https://dev.mysql.com/doc/refman/8.4/en/get-diagnostics.html), and
[routine security and parameter definitions](https://dev.mysql.com/doc/refman/8.4/en/create-procedure.html).

## Local message database

The account-scoped SQLite schema, transaction/cursor rules and deployment workflow are documented in [MessageStorage](MessageStorage.md). Server sync reuses `chat_message.client_msg_uuid` from existing migration 002; it adds no parallel identity table or MySQL migration.

## MySQL connection recovery

Gate, Chat and ResourceCatalog use `common/mysql/ConnectionPool.h` for bounded
borrowing, checkout validation and one replacement attempt before business SQL.
Connection setup and health checks run outside the pool mutex. Close wakes waiters;
checked-out work must finish before destroying the pool. Gate no longer needs a
background keepalive worker. Pool capacity remains 8 for Gate/Chat and 2 for resources.

Queue waiting defaults to two seconds. Gate/Chat connector connect/read/write
timeouts remain two seconds each; resources retain 3/5/5 seconds. Synchronous
in-flight I/O cannot be cancelled at the queue deadline,
so this is not a two-second end-to-end request guarantee. Failed business writes
are never automatically replayed. Return rolls back unfinished work and discards
connections whose reset fails; checkout replaces dead idle sessions.

Private-chat creation normalizes both query and insert participant order. Friend
confirmation, private-chat creation and resource-message writes use the existing
rollback-on-exit transaction helper, so exception cleanup cannot commit partial work.
No schema, dependency version, protocol or session-state transition changes are required.

## 服务端口令记录

新注册/改密保存带版本的 `pbkdf2-sha256$600000$<salt>$<derived>`，由 OpenSSL
PBKDF2-HMAC-SHA256 派生，盐为 16 字节安全随机值的十六进制编码，结果为 32 字节。
仍接受客户端现有不透明口令值作为输入，不改变传输编码，也不能将其视为传输加密。
旧记录只在匹配成功后按 UID 与原记录条件更新；损坏或未知哈希版本拒绝认证。
无需修改 schema；首次登录升级会写库。重置密码更新同时约束用户名与邮箱，避免检查后改名竞态。
发布包的 `migrations/` 包含可运行的版本化迁移入口，不得仅顺序执行 SQL 绕过版本和校验和登记。
