# Schema migration and registration tests

## PostgreSQL 与双库合同

设置 `CHAT_PG_BIN` 为包含 initdb 和 pg_ctl 的绝对目录，`CHAT_PG_CLIENT` 为 psql 路径，
`CHAT_MYSQL_BIN` 为 mysqld 所在目录。MySQL 客户端单独安装时用 `CHAT_MYSQL_FIXTURE_CLIENT` 指定。
运行 `node --test tests/server/schema-migration/postgres.test.js`，脚本在独占的回环端口创建临时库，
验证空库和漂移拒绝、账号比较、注册及消息事务。

Windows 使用 `tests/server/PostgresContractTests.vcxproj`，按普通测试入口指定固定 vcpkg 根和 `/p:VcpkgManifestInstall=false`；Linux 使用 CMake 目标 `postgres_contract`。将 `CHAT_PG_CONTRACT_BINARY` 指向编译出的 `postgres_contract`，同一次运行还会执行两种驱动的
原生资料、好友、回执和群合同，以及 libpq 账号锁、并发 UUID、提交前后断连、附件授权和头像用例。
将 `CHAT_DB_SERVICE_BIN` 指向包含 GateServer、ChatServer 和 ResourceServer 的绝对目录，
并提供 redis-server、redis-cli，可在两个后端各起一个网关、两个 Chat 和一个资源服务，跑注册、
选服、跨实例发送、幂等重试与补拉。此用例启动 VarifyServer 与本机 SMTP 测试对端，通过 Gate 请求验证码，再从独占 Redis 读取该验证码完成注册；需先准备 VarifyServer 的已锁定 Node.js 依赖。
Linux CI 的双库步骤同时设置两个二进制入口。

当前 PG 版本 1 从空库初始化。在发布此基线之前修改 SQL 时，执行 `node schema/postgresql/contract.js` 刷新清单及原生常量。已使用该版本的数据库遇到校验漂移会拒绝启动；后续已发布版本需增加有序升级步骤。
账号权重可以通过 `node tests/server/schema-migration/captureAccountWeights.js` 从隔离 MySQL 重新导出。

The production owner is `schema/SchemaMigration.js`; `schema/SchemaContract.h`
contains the metadata query and expected fingerprint used by both the Node tool
and native startup verification. No schema SQL is duplicated in Docker init.

Run the dependency-free owner checks:

```text
node --test tests/server/schema-migration/migration.test.js
```

The hosted coordinator calls `runMigrationCases(coordinator, record)` after its
owned MySQL database is created. It emits `T10-MIG-01..12` into the report selected
by the shared service runner. These are real MySQL migration/routine tests, not
C++ DAO or four-process workflow proof.

| ID | Real contract |
| --- | --- |
| 01 | Empty database reaches current version 7 and complete schema, including avatar/resource and message receipt tables |
| 02 | Reapply is a no-op preserving registered data |
| 03..04 | Applied checksum drift and unknown version fail closed |
| 05..06 | Missing UUID unique index or stored procedure fails verification |
| 07..08 | Eight concurrent same/distinct registrations preserve identity and uniqueness |
| 09 | Missing UID seed returns failure and does not insert a user |
| 10 | Real partial DDL failure records failed state; blind retry is rejected; explicit empty-bootstrap recovery works |
| 11 | Standalone routine comment changes preserve verification; executable body changes fail |
| 12 | Only the owned auxiliary database is dropped; primary current-N database remains for downstream tests |

For a local supplemental run, set `CHAT_MYSQL_BIN` to an existing MySQL installation's
absolute `bin` directory and run:

```text
node tests/server/schema-migration/runLocal.js
```

The fixture initializes a fresh temporary data directory, binds a random loopback
port, disables MySQL X and binary logging, waits for real SQL readiness, and shuts
down only its own server. Windows uses `--no-monitor` so a killed launcher cannot
leave an unowned forked server. Personal login files are excluded explicitly; the
fixture ignores ambient MYSQL_PWD. It never controls installed services or existing databases.
The initialization command is bounded at 60 seconds, readiness at 30 seconds,
SQL at 10 seconds, and teardown at 15 seconds. Loopback bootstrap uses an empty root
password only inside this short-lived, owned instance. Hosted uses its coordinator's
synthetic password; neither path puts credentials in argv or diagnostics.

The user-export diagnosis can be rerun with an explicitly provided local SQL path:

```text
node tests/server/schema-migration/reproduceRegistration.js <schema-only-export.sql> --diagnose
```

That command intentionally exits nonzero when the old `reg_user` returns `-1`.
The original dump is not copied into the repository and is never run on its source
database. The regression distinguishes unknown `pwd` (1054), missing required
profile values (1364), and an empty UID counter. Current SQL also pins procedure
parameter collation to the exported table collation, avoiding 1267 on fresh servers.

No promoted N-1 has been supplied: `BOOTSTRAP_NO_PROMOTED_N_MINUS_1` remains an
explicit compatibility gap. The partial bootstrap recovery is not N-1 upgrade proof.
Local MySQL 8.0 results supplement, but do not replace, the hosted MySQL 8.4 gate.
The shared metadata fingerprint removes standalone `-- ` comment text while
preserving indentation and newlines: MySQL 8.0 omits that text from
`ROUTINE_DEFINITION`, whereas 8.4 retains it. T10-MIG-01 exercises the same
checksummed migration on either version, and T10-MIG-11 distinguishes explanatory
comment changes from executable routine drift. Migration file checksums stay exact.

Current target is schema 7. T10-MIG-10 additionally upgrades a seeded schema-5
group with existing messages and checks immutable creation identity, active epoch 1,
boundary 0 and repeat safety. An ownerless legacy group, including an owner role whose user no longer exists, must fail the migration
preflight without adding membership-state columns. Only owned temporary databases are used.
