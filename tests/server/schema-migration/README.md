# Schema migration and registration tests

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
| 01 | Empty database reaches current version 2 and complete schema |
| 02 | Reapply is a no-op preserving registered data |
| 03..04 | Applied checksum drift and unknown version fail closed |
| 05..06 | Missing UUID unique index or stored procedure fails verification |
| 07..08 | Eight concurrent same/distinct registrations preserve identity and uniqueness |
| 09 | Missing UID seed returns failure and does not insert a user |
| 10 | Real partial DDL failure records failed state; blind retry is rejected; explicit empty-bootstrap recovery works |
| 11 | Changed routine body fails verification |
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
