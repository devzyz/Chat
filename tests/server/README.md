# Windows Server tests

消息持久化与增量同步的独立真实 MySQL/TCP 入口见 [message-sync](message-sync/README.md)，不计入下方快速回归数量。

`RunServerTests` is the single local/CI entry. It builds separate ChatServer Unit, Component, and Integration executables plus independent Gate/Status Asio Unit executables, all against real production sources and the already-restored vcpkg tree.

| Module | Domain | Level | Target/report |
| --- | --- | --- | --- |
| `config`, `messaging`, `transport`, `protocol`, `rpc` | Foundation | Unit | `server_unit_tests` / `server_unit.xml` |
| [message-receipt](message-receipt/README.md) | Business | Unit | `server_unit_tests` / `server_unit.xml` |
| `logic-dispatcher` | Architecture | Unit | `server_unit_tests` / `server_unit.xml` |
| `status-routing` | Business/Architecture | Unit + Component | `server_unit_tests`, `server_component_tests` / `server_unit.xml`, `server_component.xml` |
| `chat-session-state` | Architecture | Component + Integration | `server_component_tests`, `server_integration_tests` |
| `gate-request` | Business | Component | `server_component_tests` / `server_component.xml` |
| `lifecycle` | Foundation | Unit | Chat/Gate/Status Unit targets and reports |
| `data` | Foundation | Unit + Component | `server_unit_tests`, `server_component_tests` / `server_unit.xml`, `server_component.xml` |
| `gate-response` | Architecture | Component | `server_component_tests` / `server_component.xml` |
| `startup`, protocol/RPC loopback | Architecture | Integration | `server_integration_tests`, `chat_grpc_client_tests` / `server_integration.xml`, `server_chat_grpc_integration.xml` |
| `integration-host` | Architecture | Integration | `server_integration_tests` / `server_integration.xml` |
| [resource](resource/README.md) | Business | Integration | `ResourceTests` / `server_resource_integration.xml`（存储合同）；HTTP/数据库等扩展使用专项入口 |

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
```

The runner removes stale reports, uses the fixed read-only dependency tree with manifest installation disabled, and builds and app-local deploys GateServer, StatusServer, ChatServer and ResourceServer. It runs the report owners above, including ResourceStore filesystem contracts. Exact report groups and testcase counts are maintained by `scripts/windows-local.ps1`; missing reports and count drift fail the entry. CI retains `build/test-results/server_*.xml`. Resource HTTP/database and message-sync/receipt opt-in tests are documented separately and are not implied by this aggregate.
