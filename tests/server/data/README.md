# Server data-adapter tests

## Production contract and coverage

The test compiles the real `ChatServer/ChatServer/RedisMgr.*` pool implementation. A zero-sized pool is the smallest service-free configuration that exercises its public borrow/close lifecycle without opening Redis connections or supplying credentials.

| Test ID | Level | Path | Contract |
| --- | --- | --- | --- |
| T04-RDS-01 | Component | lifecycle/failure | `close()` wakes a blocked borrower, which returns null. |
| T04-RDS-02 | Component | boundary/lifecycle | Repeated close is safe and future borrows return null promptly. |

Both futures have finite one/three-second waits. The pool owns and joins its heartbeat thread. The host, port, and password are inert test placeholders; pool size zero prevents network access. No fixed sleep, public endpoint, repository credential, Redis, or MySQL instance is used.

## Local and CI execution

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=ServerRedisPoolTests.*
```

The existing `servers-release` job reuses its one vcpkg restore, links the existing hiredis dependency, and uploads `build/test-results/server_unit.xml` on success or failure. Missing XML is an error.

## RED -> GREEN evidence

RED was recorded by temporarily requiring the awakened borrower to be non-null. The unified runner exited 1 with `ServerRedisPoolTests.CloseWakesAWaitingBorrowerWithoutAServiceConnection` (actual null). The intended null assertion was restored; both data tests then pass with the full server suite.

## Known gaps (partial coverage)

- This module is intentionally partial: CI has no controlled Redis or MySQL services. Command correctness, authentication, reconnect/health checks, failed connection construction, returned-connection validation, and distributed locks remain Integration gaps.
- The MySQL connection pool and DAO require a live schema and credentials and have no existing injectable connection factory. They are not represented by fake success assertions.
- The heartbeat interval can make teardown take about one second; the required test remains bounded and service-free, but it is not a substitute for real adapter Integration tests.
