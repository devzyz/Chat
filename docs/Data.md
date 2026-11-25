# 服务端数据库、结构版本与迁移

GateServer、每个 ChatServer 实例和 ResourceServer 需要指向同一个数据库。开发配置默认 PostgreSQL。
已有单独的 `[Mysql]` 配置仍按 MySQL 读取。新配置用 `[Database]`，`Driver` 可选
`postgresql` 或 `mysql`，省略时默认 PostgreSQL。`Schema` 表示数据库名，PG 对象放在固定的
`chat` schema。新配置通过进程环境 `CHAT_DB_PASSWORD` 传递凭据，配置同时出现两个数据库节时启动失败。

## 接入现有 PostgreSQL

1. 在现有实例上准备 UTF8 空库（示例名 `chat_dev`）和账号。开发时可用该库的所有者初始化。
   在三个服务及各 Chat 实例配置中设置相同的 `Host`、`Port`、`Schema`、`User`。
2. 给迁移进程设置 `CHAT_DB_DRIVER=postgresql`、`CHAT_PG_HOST`、`CHAT_PG_PORT`、
   `CHAT_PG_DATABASE`、`CHAT_PG_USER` 和 `CHAT_PG_PASSWORD`。可选 `CHAT_PG_CLIENT` 指向
   已安装的 psql。依次执行以下命令，错误通过非零退出码返回。服务进程验证结构后才监听。

   ```text
   node schema/migrate.js plan
   node schema/migrate.js apply
   node schema/migrate.js verify
   ```

3. 为新开发库分配独立的 Redis、ResourceServer 存储目录和客户端环境（例如使用不同的 Gate 端口）。
   空库会重新分配 UID 和消息 ID，旧登录状态、文件授权和客户端同步游标属于旧环境。
   本流程只初始化空库，对现有 MySQL 数据和旧库不执行导入、删除或覆盖。

PG 初始化使用同一会话内的事务咨询锁、事务 DDL 和文件摘要。重复 apply 核对版本、结构指纹和 UID
计数器。未知非空库或结构漂移会使 apply 失败。运行账号需能读取业务表、账号权重与结构元数据，
执行相应业务函数及 CRUD，并使用 identity 序列。应用本身不执行 DDL。

账号保留原始字节；唯一索引和按名/邮箱查找使用从 MySQL `utf8mb4_german2_ci` 获取的主权重。
例如 `ß` 与 `ss` 相等，两个不同的补充平面字符也可能相等。Gate 核验改密身份时先按用户名键定位，
再逐字节核对原邮箱。消息正文使用 bytea，支持零字节，重试逐字节比较。
认证锁与连接共用生命周期，PG 需要直连或使用支持会话级锁的连接方式。
参见[双库合同与测试命令](../tests/server/schema-migration/README.md#postgresql-与双库合同)。

## MySQL 既有迁移

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

Gate, Chat and ResourceCatalog use `common/database/ConnectionPool.h` for bounded
borrowing, checkout validation and one replacement attempt before business SQL.
Connection setup and health checks run outside the pool mutex. Close wakes waiters;
checked-out work must finish before destroying the pool. Gate no longer needs a
background keepalive worker. Pool capacity remains 8 for Gate/Chat and 2 for resources.

For clients without `basic_social_v1`, ChatServer builds the login response's
`friend_list` with one connection and one join of `friend` to `user`. The
profile comes from the friend's user row, and `backname` comes from the
relation owned by the logging-in user.

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

## 认证状态与恢复

| Redis 记录 | 写入者与有效期 | 续期、撤销或清理 |
| --- | --- | --- |
| 账号凭据：以 UID 为键的哈希，字段 `utoken_<uid>`，另有 `auth_version_<uid>` | Gate 比较认证代次后签发当前 Token，空闲期限 24 小时。撤销代次独立保留 | Chat 校验当前会话并匹配绑定 Token 后续期。Gate 改密或成功撤销当前登录时提升代次、删除 Token |
| 用户在线位置：`uip_<uid>`、`usessionid_<uid>` 和绑定凭据 `usessiontoken_<uid>` | Chat 的 `RedisUserPresenceStore` 校验账号 Token，原子发布实例名、Session ID 和绑定 Token，三键均为 90 秒 | 校验当前 Session ID、位置存在且绑定 Token 仍与账号 Token 匹配后续期。清理时同时比较实例名和 Session ID，匹配才删除三键 |
| Gate 选服：`chatlease_<实例名>` 和 `logincount` 中的实例字段 | Chat 的 `InstanceLease` 登记进程 owner，租约 90 秒，定时器每 60 秒提交一次续租及连接数更新 | Gate 读取租约与有效计数选服。续租遇到其他 owner 会放弃更新，注销只删除匹配 owner 的租约和计数；租约过期的实例退出候选 |

用户在线记录按连接续期，实例租约按进程续期，两者可独立过期。[凭据签发与选服](../GateServer/GateServer/GateRequestProduction.cpp) · [在线位置](../ChatServer/ChatServer/RedisUserPresenceStore.cpp) · [实例租约](../ChatServer/ChatServer/InstanceLease.h)

Gate 在账号级数据库会话锁内完成登录校验和选服，或撤销和密码更新。MySQL 使用 `chat-auth-<uid>` 命名锁，PostgreSQL 使用按 UID 的咨询锁。两种实现都独占借出的数据库连接，只有确认解锁才归还可复用的连接；解锁结果不明时断开连接。[MySQL 账号锁](../GateServer/GateServer/MysqlDao.cpp) · [PostgreSQL 账号锁](../GateServer/GateServer/PostgresAccounts.h)

备份恢复不能把旧 Redis 凭据直接恢复为有效登录。恢复操作需停止服务、处理旧认证缓存并强制重新登录；
MySQL、资源目录与持久化 Redis 的一致备份恢复演练仍未完成，不能将仅复制数据库称为完整恢复。
