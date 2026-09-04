# Windows Server tests

`RunServerTests` is the single local/CI entry. It builds separate ChatServer Unit, Component, and Integration executables plus independent Gate/Status Asio Unit executables, all against real production sources and the already-restored vcpkg tree.

| Module | Domain | Level | Target/report |
| --- | --- | --- | --- |
| `config`, `messaging`, `transport`, `protocol`, `rpc` | Foundation | Unit | `server_unit_tests` / `server_unit.xml` |
| `logic-dispatcher` | Architecture | Unit | `server_unit_tests` / `server_unit.xml` |
| `status-routing` | Business/Architecture | Unit + Component | `server_unit_tests`, `server_component_tests` / `server_unit.xml`, `server_component.xml` |
| `chat-session-state` | Architecture | Component | `server_component_tests` / `server_component.xml` |
| `gate-request` | Business | Component | `server_component_tests` / `server_component.xml` |
| `lifecycle` | Foundation | Unit | Chat/Gate/Status Unit targets and reports |
| `data`, `gate-response` | Foundation/Architecture | Component | `server_component_tests` / `server_component.xml` |
| `startup`, protocol/RPC loopback | Architecture | Integration | `server_integration_tests`, `chat_grpc_client_tests` / `server_integration.xml`, `server_chat_grpc_integration.xml` |
| `integration-host` | Architecture | Integration | `server_integration_tests` / `server_integration.xml` |

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
```

The runner removes stale files, uses the fixed read-only dependency tree with manifest installation disabled, builds and app-local deploys all three production executables, and writes `server_unit.xml`, `server_component.xml`, `server_integration.xml`, `server_chat_grpc_integration.xml`, `server_gate_unit.xml`, and `server_status_unit.xml`. It rejects missing reports and exact testcase-count drift (68 Unit, 56 Component, 40 main Integration, 4 Chat gRPC Integration, and 2 for each Gate/Status lifecycle target), for 172 Server cases total. CI always uploads `build/test-results/server_*.xml`; a missing or count-drifted report is an error.
