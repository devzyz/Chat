# Message synchronization Integration tests

Production implementation: `common/message/MessagePersistence.h`, used by
ChatServer's `MysqlDao`; TCP handler is `LogicSystem.cpp`.

| Test ID | Test | Contract |
| --- | --- | --- |
| S06-SYNC-01 | RetryConflictAndBatchRollback | UUID retry returns original ID; conflicting content rolls back the whole batch; participant check |
| S06-SYNC-02 | IncrementalByteBoundedPagesAndResourceIdentity | Incremental cursor, byte-bounded pages, empty catch-up, resource UUID, text/resource identity conflicts and membership |
| S06-SYNC-03 | CursorCannotPassAnUncommittedWriter | Observe real InnoDB lock wait; sync sees the committed earlier message |
| S06-SYNC-04 | integration.py / tcp_flow | Offline send ACK, retry, two production ChatServer instances, push, incremental fetch and relogin |
| S06-SYNC-05 | integration.py / message_sync_probe | Real Qt coordinator and SQLite; process restart requests only IDs after the persisted cursor |

Use existing dependencies only. From a VS developer shell with `VCPKG_ROOT` set:

```powershell
msbuild ChatServer/ChatServer/ChatServer.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VcpkgManifestInstall=false
msbuild tests/server/message-sync/MessageSyncTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VcpkgManifestInstall=false
msbuild tests/server/resource/ResourceTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VcpkgManifestInstall=false
cmake --build build/windows-client/Release --target chat message_sync_probe
python -B tests/server/message-sync/integration.py
```

The Qt Release build must already be configured; the probe uses its SQL/Network runtime
and QSQLITE plugin (ensure the selected Qt/MinGW `bin` directories are on PATH).
The Python entry creates its own MySQL data directory under `build/message-sync`,
applies migrations twice, runs production persistence tests, starts two ChatServer
processes, and cleans up only its own processes/files. `mysql` and `mysqld` must
already be on PATH. Binaries need their app-local runtime dependencies.
The existing ResourceTests executable provides the Status fixture; Redis also
uses the existing deterministic fixture. This is real MySQL/TCP/gRPC evidence,
not production Status/Redis or a full desktop E2E claim.

MySQL results: `build/message-sync/mysql.xml`; TCP failures propagate as nonzero
exit. These opt-in Integration cases are outside the thirteen-report quick CI lane.
