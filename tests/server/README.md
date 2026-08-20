# Windows Server tests

`RunServerTests` is the single local/CI entry. It builds the ChatServer test executable plus independent Gate/Status Asio lifecycle executables, all against real production sources and the already-restored vcpkg tree.

| Module | Directory | Scope |
| --- | --- | --- |
| Configuration | `config` | INI selection and validation |
| Startup | `startup` | ChatServer subprocess CLI/bind failure lifecycle |
| Messaging/transport | `messaging`, `transport` | Buffer ownership and production frame validation |
| Concurrency/lifecycle | `concurrency`, `lifecycle` | Chat/Gate/Status task dispatch and idempotent shutdown |
| Protocol/RPC | `protocol`, `rpc` | protobuf contracts and route mapping |
| Data | `data` | service-free Redis pool close/finite borrow |

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
```

The runner removes stale files, reuses the one Server dependency restore, and writes `server_unit.xml`, `server_gate_asio.xml`, and `server_status_asio.xml`. CI always uploads `build/test-results/server_*.xml`; a missing report is an error. Startup tests are process Integration tests; the remaining in-process modules follow their own README classifications.
