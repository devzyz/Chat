# MySQL schema and migration contract

The schema source is the user-supplied schema-only export of the historical Chat
development database (nine tables and `reg_user`). The raw export, its host metadata,
and its former AUTO_INCREMENT counters are not shipped. Executable ownership is
[`schema/migrations`](../schema/migrations), with ordered SHA-256 checksums and
recovery descriptions in [`manifest.json`](../schema/manifest.json).

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

`Inspect` reports schema/history; `Plan` refuses unknown versions, checksum drift,
partial history, and unversioned nonempty databases. `Apply` uses a bounded advisory
lock on a single real session, records applying/applied/failed states in
`schema_version`, then verifies semantic table/column/index/routine/trigger metadata.
`Verify` is read-only apart from session settings and fails on missing or altered
objects. Native Gate/Chat startup calls `chat_schema::Verify` from the same metadata
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

The export's `group_chat_member` has only `chat_id` as its primary key, restricting
it to one member per group. This pre-existing group-model limitation is preserved,
not presented as implemented group chat. No unproven foreign keys or triggers were
invented from DAO queries.

For executed evidence and follow-up scope use the main workspace `docs/Status.md`.
The [schema owner tests](../tests/server/schema-migration/README.md) are separate from
MessageCommit's native transaction/permission tests and four-process integration.

MySQL references: [implicit DDL commits](https://dev.mysql.com/doc/refman/8.4/en/implicit-commit.html),
[session advisory locks](https://dev.mysql.com/doc/refman/8.4/en/locking-functions.html),
[SQL diagnostics](https://dev.mysql.com/doc/refman/8.4/en/get-diagnostics.html), and
[routine security and parameter definitions](https://dev.mysql.com/doc/refman/8.4/en/create-procedure.html).
