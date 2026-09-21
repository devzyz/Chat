# Message commit integration

`MessageCommit` validates an authenticated sender independently of the claimed payload sender,
canonical lower-case UUIDs, duplicate batch identities and the request deadline. The production
`MySqlMessageCommitAdapter` validates the current private-chat participants, then executes one
explicit transaction. A duplicate `(send_id, client_msg_uuid)` returns `EXISTING` and the same
server ID only when chat, recipient and exact content match. Every failure clears the result batch
and rolls back. Group-message delivery is not introduced by this private-message API.

`MysqlDao::AddChatMessageList` delegates to that module; history loads persisted UUIDs and Unix
timestamps. Legacy rows retain NULL UUIDs and are still readable. Notification is after confirmed
commit, not a claim of exactly-once network delivery. A lost COMMIT acknowledgement is ambiguous:
the adapter reports storage unavailable, discards the connection, and retrying the original UUID
resolves the outcome without duplicate persistence.

The classic connector sets two-second connect/read/write and InnoDB lock-wait bounds. Deadlines
are checked before transaction work and between commands, not simulated with a detached timeout
thread. An in-flight synchronous command and rollback may consume their connector timeout beyond
the application deadline; a confirmed COMMIT remains successful. Numeric IP endpoints (or localhost)
are required because synchronous system DNS cannot be cancelled. The Chat pool has finite borrows,
close wakeups, bounded replacement of failed sessions and no detached heartbeat worker.

## Run

Build `MessageCommitTests.vcxproj` with the repository's existing read-only dependency configuration,
or build the CMake `message_commit_integration` target. `message_commit_integration validation`
is the no-database fast check. The executable and ChatServer link the same `MessageCommit` library.

For isolated local integration, set `CHAT_MYSQL_BIN` to an already installed MySQL 8.0 bin directory
and `CHAT_MESSAGE_TEST_BINARY` to the absolute built executable, then run:

```text
node tests/server/message-commit/runLocal.js
```

This creates and owns a fresh mysqld data directory, port and database, and closes the instance in
`finally`. It does not connect to a development database or restore local dependencies. Hosted
`runMessageCases(coordinator, record)` uses the existing owned MySQL container and mapped TCP port.
The persistent migration session uses its native mysql client; actual transactions use the production
C++ connector. The auxiliary database derives from the RunContext name and is explicitly dropped.
Numeric-only diagnostics and test IDs are emitted, not SQL data or credentials.

## Contracts

| IDs | Contract |
| --- | --- |
| T10-MSG-01 | Canonical UUID and validation before storage. |
| T10-MSG-02..05 | Created, Existing, exact-content and cross-chat/recipient conflict. |
| T10-MSG-06..09 | Second-item rollback, same-batch duplicate, absent and forged sender. |
| T10-MSG-10..12 | Invalid recipient/member and expired deadline. |
| T10-MSG-13 | Two simultaneous real connections: one row, one ID, Created/Existing. |
| T10-MSG-14 | All inserts run, then an independent connection kills the transaction connection at the commit boundary; real commit fails and no rows persist. |
| T10-MSG-15..18 | Autocommit restoration, SQL failure rollback, legacy NULL UUIDs and increasing server IDs. |
| T10-MSG-19 | Deterministic row-lock contention terminates within the bounded timeout without a write. |
| T10-MSG-20 | Closed storage rejects, child exits and owned database cleanup are all required. |

The commit-boundary dependency delegates directly to `sql::Connection::commit()` in production;
only integration supplies the independent-session fault injector. It does not replace SQL or the
transaction algorithm. Each native run has a 45-second parent process deadline and bounded output.
The 20 results populate `linux_message.xml`; they are not counted as the existing Windows
component cases. Public TCP/Qt identity checks belong to their corresponding tests and Phase 3D
still owns the two-ChatServer public E2E gate.
