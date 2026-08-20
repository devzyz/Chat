# Concurrency/lifecycle tests

## Production contract and coverage

`GateServer/GateServer/AsioIOServicePool.*` and `StatusServer/StatusServer/AsioIOServicePool.*` are compiled directly into the test target with isolated type names. Their current stable contract is that queued work runs on a pool worker and completed work is not lost when the singleton is destroyed.

| Test ID | Level | Path | Contract |
| --- | --- | --- | --- |
| T03-GATE-01 | Component | normal/lifecycle | Gate pool dispatches queued work before destructor shutdown. |
| T03-STATUS-01 | Component | normal/lifecycle | Status pool dispatches queued work before destructor shutdown. |

This does not repeat the earlier ChatServer pool test; it exercises the two other implementations. Each case uses a condition variable with a five-second upper bound and no fixed sleep. Singleton destruction owns thread join/cleanup; no process, port, or external dependency is created.

## Local and CI execution

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=ServerAsioPoolLifecycleTests.*
```

The existing `servers-release` CI job writes and uploads `build/test-results/server_unit.xml`, including on failure; a missing artifact is an error.

## RED -> GREEN evidence

RED was recorded by temporarily negating the bounded completion assertions. The unified runner exited 1 and both `GatePoolExecutesQueuedWorkBeforeDestructorShutdown` and `StatusPoolExecutesQueuedWorkBeforeDestructorShutdown` failed. The assertions were restored and both tests passed.

## Known gaps

- Unlike ChatServer's implementation, Gate and Status `stop()` call `join()` on every invocation and are not safely repeatable. An idempotent-stop required test would currently expose a production bug, so it is recorded here instead of asserting the wrong behavior.
- Queue saturation, startup rollback, exception propagation, and long-running stress behavior need a separately specified concurrency test environment.
